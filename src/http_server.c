/*
 * HTTP Server — Implementation
 *
 * Socket-based HTTP server using Zephyr BSD sockets.
 * Architecture: listener thread accepts connections, serves static
 * pages inline, hands MJPEG/WebSocket stream to a dedicated worker.
 * Max 1 concurrent stream client (TCP or WebSocket).
 */

#include "http_server.h"
#include "frame_source.h"
#include "led_control.h"
#include "wifi_manager.h"
#include "html_pages.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/base64.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/sys/mem_stats.h>

LOG_MODULE_REGISTER(http_srv, LOG_LEVEL_INF);

#define HTTP_LISTEN_BACKLOG  2
#define HTTP_RECV_BUF_SIZE   1024
#define HTTP_SEND_TIMEOUT_MS  5000
#define STREAM_SEND_TIMEOUT_S 5    /* Tolerant of WiFi latency spikes */
#define STREAM_CLOSE_COOLDOWN_S 3  /* Wait for TCP retransmit drain after stream close */
#define MJPEG_FRAME_DELAY_MS 50    /* Minimal delay; capture itself takes time */
#define MJPEG_BOUNDARY       "frame"
#define WS_FRAME_DELAY_MS    50

#define HTTP_THREAD_STACK_SIZE  3072
#define STREAM_THREAD_STACK_SIZE 3072
#define HTTP_THREAD_PRIORITY    7
#define STREAM_THREAD_PRIORITY  8   /* Lower priority than listener */

enum stream_mode {
	STREAM_MJPEG_TCP,
	STREAM_WS_BINARY,
	STREAM_SNAPSHOT,
};

static int server_port;
static int stream_port;  /* Separate port for streams (server_port + 1) */

/* Stream worker: socket fd + mode passed via semaphore */
static struct k_sem stream_sem;
static volatile int stream_client_fd = -1;
static volatile enum stream_mode stream_mode;
static char ws_pending_key[25]; /* WS key extracted by listener, used by stream */

/* Telemetry: updated by stream thread, read by API */
static volatile uint32_t stream_fps10;     /* FPS × 10 */
static volatile uint32_t stream_frame_cnt; /* total frames this session */

static const char http_503[] =
	"HTTP/1.1 503 Service Unavailable\r\n"
	"Content-Type: text/plain\r\n"
	"Connection: close\r\n"
	"Content-Length: 24\r\n"
	"\r\n"
	"Stream already in use.\r\n";

static const char http_200_html_hdr[] =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: text/html; charset=utf-8\r\n"
	"Connection: close\r\n"
	"Content-Length: ";

static const char http_mjpeg_hdr[] =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: multipart/x-mixed-replace; boundary=" MJPEG_BOUNDARY "\r\n"
	"Cache-Control: no-store, no-cache, must-revalidate\r\n"
	"Pragma: no-cache\r\n"
	"Connection: close\r\n"
	"\r\n";

static const char http_404[] =
	"HTTP/1.1 404 Not Found\r\n"
	"Content-Type: text/plain\r\n"
	"Connection: close\r\n"
	"Content-Length: 9\r\n"
	"\r\n"
	"Not Found";

static const char http_200_json_hdr[] =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: application/json\r\n"
	"Access-Control-Allow-Origin: *\r\n"
	"Connection: close\r\n"
	"Content-Length: ";

/* Send all bytes, handling partial writes */
static int sendall(int sock, const void *buf, size_t len)
{
	const uint8_t *p = buf;

	while (len > 0) {
		ssize_t sent = zsock_send(sock, p, len, 0);

		if (sent < 0) {
			return -errno;
		}
		p += sent;
		len -= sent;
	}
	return 0;
}

/*
 * Wait for client to initiate TCP close before we call zsock_close().
 * Server-initiated FIN on Zephyr/ESP32 corrupts the networking stack,
 * so we let the client close first (they honor our "Connection: close" header).
 */
static void wait_for_peer_close(int fd, int timeout_ms)
{
	struct zsock_pollfd pfd = { .fd = fd, .events = ZSOCK_POLLIN };
	int ret = zsock_poll(&pfd, 1, timeout_ms);

	if (ret > 0 && (pfd.revents & (ZSOCK_POLLIN | ZSOCK_POLLHUP))) {
		char drain[64];
		ssize_t n = zsock_recv(fd, drain, sizeof(drain), 0);

		LOG_INF("peer_close fd=%d: recv=%zd", fd, n);
	} else if (ret == 0) {
		LOG_WRN("peer_close fd=%d: timeout %dms", fd, timeout_ms);
	} else {
		LOG_ERR("peer_close fd=%d: poll err %d", fd, ret);
	}
}
static int parse_request(const char *buf, char *method, size_t method_len,
			 char *path, size_t path_len)
{
	const char *p = buf;
	size_t i = 0;

	/* Extract method */
	while (*p && *p != ' ' && i < method_len - 1) {
		method[i++] = *p++;
	}
	method[i] = '\0';

	if (*p != ' ') {
		return -EINVAL;
	}
	p++;

	/* Extract path */
	i = 0;
	while (*p && *p != ' ' && *p != '?' && i < path_len - 1) {
		path[i++] = *p++;
	}
	path[i] = '\0';

	return 0;
}

/* Case-insensitive header search in HTTP request buffer */
static const char *find_header(const char *buf, const char *name)
{
	size_t nlen = strlen(name);
	const char *p = buf;

	while ((p = strchr(p, '\n')) != NULL) {
		p++;
		/* Case-insensitive prefix match */
		bool match = true;
		for (size_t i = 0; i < nlen; i++) {
			char a = p[i];
			char b = name[i];

			if (a >= 'A' && a <= 'Z') {
				a += 32;
			}
			if (b >= 'A' && b <= 'Z') {
				b += 32;
			}
			if (a != b) {
				match = false;
				break;
			}
		}
		if (match) {
			p += nlen;
			while (*p == ' ' || *p == '\t') {
				p++;
			}
			return p;
		}
	}
	return NULL;
}

/* WebSocket GUID for handshake (RFC 6455) */
static const char ws_guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/*
 * Minimal SHA-1 — all working buffers are stack-local to keep DRAM free.
 * Functions marked noinline to prevent the compiler from merging their
 * large local arrays (W[80]=320B, buf[128]) into a single mega-frame
 * in ws_handshake, which would overflow the 3072-byte stream thread stack.
 */
static void __attribute__((noinline)) sha1_block(uint32_t state[5],
						 const uint8_t blk[64])
{
	uint32_t W[80];

	for (int i = 0; i < 16; i++) {
		W[i] = ((uint32_t)blk[i * 4] << 24) |
		       ((uint32_t)blk[i * 4 + 1] << 16) |
		       ((uint32_t)blk[i * 4 + 2] << 8) |
		       (uint32_t)blk[i * 4 + 3];
	}
	for (int i = 16; i < 80; i++) {
		uint32_t t = W[i - 3] ^ W[i - 8] ^ W[i - 14] ^ W[i - 16];
		W[i] = (t << 1) | (t >> 31);
	}

	uint32_t a = state[0], b = state[1], c = state[2];
	uint32_t d = state[3], e = state[4];

	for (int i = 0; i < 80; i++) {
		uint32_t f, k;

		if (i < 20) {
			f = (b & c) | ((~b) & d);
			k = 0x5A827999;
		} else if (i < 40) {
			f = b ^ c ^ d;
			k = 0x6ED9EBA1;
		} else if (i < 60) {
			f = (b & c) | (b & d) | (c & d);
			k = 0x8F1BBCDC;
		} else {
			f = b ^ c ^ d;
			k = 0xCA62C1D6;
		}

		uint32_t tmp = ((a << 5) | (a >> 27)) + f + e + k + W[i];

		e = d;
		d = c;
		c = (b << 30) | (b >> 2);
		b = a;
		a = tmp;
	}

	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;
}

static void __attribute__((noinline)) ws_sha1(const uint8_t *msg, size_t len,
					      uint8_t out[20])
{
	uint8_t buf[128];
	uint32_t state[5] = {
		0x67452301, 0xEFCDAB89, 0x98BADCFE,
		0x10325476, 0xC3D2E1F0
	};

	memcpy(buf, msg, len);
	buf[len] = 0x80;

	/* Padded length: next multiple of 64 with room for 8-byte length */
	size_t pad_len = (len + 9 <= 64) ? 64 : 128;

	memset(buf + len + 1, 0, pad_len - len - 1);

	/* Append bit length as big-endian 64-bit */
	uint32_t bit_len = (uint32_t)(len * 8);

	buf[pad_len - 4] = (bit_len >> 24) & 0xFF;
	buf[pad_len - 3] = (bit_len >> 16) & 0xFF;
	buf[pad_len - 2] = (bit_len >> 8) & 0xFF;
	buf[pad_len - 1] = bit_len & 0xFF;

	for (size_t i = 0; i < pad_len; i += 64) {
		sha1_block(state, buf + i);
	}

	for (int i = 0; i < 5; i++) {
		out[i * 4]     = (state[i] >> 24) & 0xFF;
		out[i * 4 + 1] = (state[i] >> 16) & 0xFF;
		out[i * 4 + 2] = (state[i] >> 8) & 0xFF;
		out[i * 4 + 3] = state[i] & 0xFF;
	}
}

/*
 * Perform WebSocket upgrade handshake.
 * Takes pre-extracted Sec-WebSocket-Key, computes SHA-1 + Base64 accept value,
 * sends 101 Switching Protocols response.
 * Uses static buffers for SHA-1 to stay within 3072-byte stream thread stack.
 * Returns 0 on success, negative on error.
 */
static int ws_handshake(int client, const char *key)
{
	if (strlen(key) < 16) {
		LOG_ERR("WS: key too short (%zu)", strlen(key));
		return -EINVAL;
	}

	/* Concatenate key + GUID and compute SHA-1 */
	char concat[64]; /* 24-byte key + 36-byte GUID + NUL = 61 */
	int clen = snprintf(concat, sizeof(concat), "%s%s", key, ws_guid);

	unsigned char sha1_hash[20];

	ws_sha1((const uint8_t *)concat, clen, sha1_hash);

	/* Base64 encode the SHA-1 hash */
	char accept_b64[32];
	size_t olen;
	int ret = base64_encode(accept_b64, sizeof(accept_b64), &olen,
				sha1_hash, sizeof(sha1_hash));

	if (ret) {
		LOG_ERR("WS: base64 encode failed: %d", ret);
		return -EINVAL;
	}
	accept_b64[olen] = '\0';

	/* Send 101 Switching Protocols */
	char resp[200];
	int rlen = snprintf(resp, sizeof(resp),
		"HTTP/1.1 101 Switching Protocols\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Accept: %s\r\n"
		"\r\n", accept_b64);

	ret = sendall(client, resp, rlen);
	if (ret) {
		return ret;
	}

	LOG_INF("WS handshake complete (key=%s)", key);
	return 0;
}

/*
 * Send a WebSocket binary frame (opcode 0x82, server-to-client, no mask).
 * Handles extended payload length for frames up to 65535 bytes.
 */
static int ws_send_binary(int client, const void *data, size_t len)
{
	uint8_t hdr[4];
	size_t hdr_len;

	/* FIN=1, opcode=binary(2) */
	hdr[0] = 0x82;

	if (len < 126) {
		hdr[1] = (uint8_t)len;
		hdr_len = 2;
	} else {
		/* 16-bit extended length */
		hdr[1] = 126;
		hdr[2] = (len >> 8) & 0xFF;
		hdr[3] = len & 0xFF;
		hdr_len = 4;
	}

	int ret = sendall(client, hdr, hdr_len);
	if (ret) {
		return ret;
	}

	return sendall(client, data, len);
}

static int handle_api_status(int client, char *buf, size_t buf_size)
{
	uint32_t uptime_s = (uint32_t)(k_uptime_get() / 1000);
	/* Simulated temperature: 25.0 ± 3.0°C (integer tenths) */
	uint32_t rng = sys_rand32_get();
	int temp10 = 250 + (int)(rng % 61) - 30; /* 220..280 → 22.0..28.0 */
	bool active = (stream_client_fd >= 0);
	const char *led_str = led_control_get_state() ? "on" : "off";
	const char *led_mode = led_control_is_manual() ? "manual" : "auto";

	/* System heap stats */
	extern struct k_heap _system_heap;
	struct sys_memory_stats heap_stats;
	uint32_t heap_free = 0, heap_used = 0;

	if (sys_heap_runtime_stats_get(&_system_heap.heap, &heap_stats) == 0) {
		heap_free = (uint32_t)heap_stats.free_bytes;
		heap_used = (uint32_t)heap_stats.allocated_bytes;
	}

	/* WiFi link info */
	int rssi = 0;
	unsigned int channel = 0;
	uint32_t wifi_dc = 0, wifi_rc = 0;

	wifi_manager_get_link_info(&rssi, &channel);
	wifi_manager_get_stats(&wifi_dc, &wifi_rc);

	int json_len = snprintf(buf, buf_size,
		"{\"fps\":%u,\"uptime\":%u,\"temp\":%d,"
		"\"led\":\"%s\",\"led_mode\":\"%s\","
		"\"stream\":%s,\"frames\":%u,"
		"\"heap_free\":%u,\"heap_used\":%u,"
		"\"rssi\":%d,\"channel\":%u,"
		"\"wifi_disconnects\":%u,\"wifi_reconnects\":%u}",
		stream_fps10, uptime_s, temp10,
		led_str, led_mode,
		active ? "true" : "false",
		stream_frame_cnt,
		heap_free, heap_used,
		rssi, channel,
		wifi_dc, wifi_rc);

	/* Build HTTP header in a small temp area after the JSON */
	char hdr[128];
	int hdr_len = snprintf(hdr, sizeof(hdr), "%s%d\r\n\r\n",
			       http_200_json_hdr, json_len);
	int ret = sendall(client, hdr, hdr_len);

	if (ret) {
		return ret;
	}
	return sendall(client, buf, json_len);
}

static int handle_api_led(int client, const char *action, char *buf,
			  size_t buf_size)
{
	if (strcmp(action, "on") == 0) {
		led_control_set(true);
	} else if (strcmp(action, "off") == 0) {
		led_control_set(false);
	} else if (strcmp(action, "toggle") == 0) {
		led_control_toggle();
	} else if (strcmp(action, "auto") == 0) {
		led_control_auto();
	} else {
		sendall(client, http_404, sizeof(http_404) - 1);
		return 0;
	}

	const char *state = led_control_get_state() ? "on" : "off";
	const char *mode = led_control_is_manual() ? "manual" : "auto";
	int json_len = snprintf(buf, buf_size,
		"{\"led\":\"%s\",\"mode\":\"%s\"}", state, mode);

	char hdr[128];
	int hdr_len = snprintf(hdr, sizeof(hdr), "%s%d\r\n\r\n",
			       http_200_json_hdr, json_len);
	int ret = sendall(client, hdr, hdr_len);

	if (ret) {
		return ret;
	}
	return sendall(client, buf, json_len);
}

static int handle_index(int client)
{
	char len_str[16];
	int ret;

	snprintf(len_str, sizeof(len_str), "%zu\r\n\r\n",
		 sizeof(index_html) - 1);

	ret = sendall(client, http_200_html_hdr, sizeof(http_200_html_hdr) - 1);
	if (ret) {
		return ret;
	}

	ret = sendall(client, len_str, strlen(len_str));
	if (ret) {
		return ret;
	}

	return sendall(client, index_html, sizeof(index_html) - 1);
}

static int handle_ws_page(int client)
{
	char len_str[16];
	int ret;

	snprintf(len_str, sizeof(len_str), "%zu\r\n\r\n",
		 sizeof(ws_page_html) - 1);

	ret = sendall(client, http_200_html_hdr, sizeof(http_200_html_hdr) - 1);
	if (ret) {
		return ret;
	}

	ret = sendall(client, len_str, strlen(len_str));
	if (ret) {
		return ret;
	}

	return sendall(client, ws_page_html, sizeof(ws_page_html) - 1);
}

static int __attribute__((noinline)) handle_mjpeg_stream(int client)
{
	char part_hdr[128];
	const uint8_t *frame_data;
	size_t frame_size;
	int ret;
	uint32_t frame_count = 0;
	int64_t stream_start = k_uptime_get();

	/* Use longer send timeout for streaming */
	struct timeval tv = { .tv_sec = STREAM_SEND_TIMEOUT_S };
	zsock_setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	LOG_INF("MJPEG stream: sending header (%zu bytes) to fd=%d",
		sizeof(http_mjpeg_hdr) - 1, client);

	/* Send MJPEG response header */
	ret = sendall(client, http_mjpeg_hdr, sizeof(http_mjpeg_hdr) - 1);
	if (ret) {
		LOG_ERR("MJPEG hdr send failed: %d (fd=%d)", ret, client);
		return ret;
	}

	LOG_INF("MJPEG stream started, header sent OK");

	/* Stream frames until client disconnects */
	while (1) {
		int64_t cap_start = k_uptime_get();

		if (frame_count == 0) {
			LOG_INF("Capturing first frame...");
		}
		ret = frame_source_get(&frame_data, &frame_size);
		if (ret) {
			LOG_ERR("Failed to get frame: %d (after %u frames)",
				ret, frame_count);
			break;
		}

		int64_t cap_ms = k_uptime_get() - cap_start;

		/* MJPEG part header */
		int hdr_len = snprintf(part_hdr, sizeof(part_hdr),
			"--" MJPEG_BOUNDARY "\r\n"
			"Content-Type: image/jpeg\r\n"
			"Content-Length: %zu\r\n"
			"\r\n", frame_size);

		int64_t send_start = k_uptime_get();

		ret = sendall(client, part_hdr, hdr_len);
		if (ret) {
			LOG_ERR("Part hdr send failed: %d", ret);
			break;
		}

		ret = sendall(client, frame_data, frame_size);
		if (ret) {
			LOG_ERR("Frame send failed: %d (%zu B)", ret,
				frame_size);
			break;
		}

		ret = sendall(client, "\r\n", 2);
		if (ret) {
			LOG_ERR("Boundary send failed: %d", ret);
			break;
		}

		int64_t send_ms = k_uptime_get() - send_start;

		frame_count++;
		int64_t elapsed = k_uptime_get() - stream_start;
		int fps10 = elapsed > 0
			? (int)(frame_count * 10000 / elapsed) : 0;

		/* Update global telemetry for /api/status */
		stream_fps10 = fps10;
		stream_frame_cnt = frame_count;

		if (frame_count <= 3 || (frame_count % 20) == 0) {
			LOG_INF("Frame %u: %zu B, cap=%lld ms, "
				"send=%lld ms, avg %d.%d fps",
				frame_count, frame_size,
				cap_ms, send_ms,
				fps10 / 10, fps10 % 10);
		}

		k_msleep(MJPEG_FRAME_DELAY_MS);
	}

	int64_t elapsed = k_uptime_get() - stream_start;

	LOG_INF("MJPEG stream ended: %u frames in %lld ms",
		frame_count, elapsed);
	return 0;
}

/* WebSocket binary stream — sends JPEG frames as WS binary messages */
static int __attribute__((noinline)) handle_ws_stream(int client)
{
	const uint8_t *frame_data;
	size_t frame_size;
	int ret;
	uint32_t frame_count = 0;
	int64_t stream_start = k_uptime_get();

	/* Use longer send timeout for streaming */
	struct timeval tv = { .tv_sec = STREAM_SEND_TIMEOUT_S };
	zsock_setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	LOG_INF("WS stream started");

	while (1) {
		int64_t cap_start = k_uptime_get();

		ret = frame_source_get(&frame_data, &frame_size);
		if (ret) {
			LOG_ERR("WS: frame error: %d", ret);
			break;
		}

		int64_t cap_ms = k_uptime_get() - cap_start;
		int64_t send_start = k_uptime_get();

		ret = ws_send_binary(client, frame_data, frame_size);
		if (ret) {
			break;
		}

		int64_t send_ms = k_uptime_get() - send_start;

		frame_count++;
		int64_t elapsed = k_uptime_get() - stream_start;
		int fps10 = elapsed > 0
			? (int)(frame_count * 10000 / elapsed) : 0;

		stream_fps10 = fps10;
		stream_frame_cnt = frame_count;

		if (frame_count <= 3 || (frame_count % 20) == 0) {
			LOG_INF("WS frame %u: %zu B, cap=%lld ms, "
				"send=%lld ms, avg %d.%d fps",
				frame_count, frame_size,
				cap_ms, send_ms,
				fps10 / 10, fps10 % 10);
		}

		k_msleep(WS_FRAME_DELAY_MS);
	}

	int64_t elapsed = k_uptime_get() - stream_start;

	LOG_INF("WS stream ended: %u frames in %lld ms",
		frame_count, elapsed);
	return 0;
}

/* Single-frame snapshot — captures one JPEG and serves it as image/jpeg.
 * Runs on the stream thread to share the camera serialization with streams.
 */
static int __attribute__((noinline)) handle_snapshot(int client)
{
	const uint8_t *frame_data;
	size_t frame_size;
	char hdr[160];
	int ret;
	int64_t t0 = k_uptime_get();

	struct timeval tv = { .tv_sec = STREAM_SEND_TIMEOUT_S };
	zsock_setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	ret = frame_source_get(&frame_data, &frame_size);
	if (ret || frame_size == 0) {
		LOG_ERR("Snapshot: capture failed (%d)", ret);
		sendall(client, http_503, sizeof(http_503) - 1);
		return ret ? ret : -EIO;
	}

	int hdr_len = snprintf(hdr, sizeof(hdr),
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: image/jpeg\r\n"
		"Content-Length: %zu\r\n"
		"Content-Disposition: inline; filename=\"snapshot.jpg\"\r\n"
		"Cache-Control: no-store\r\n"
		"Connection: close\r\n\r\n",
		frame_size);

	ret = sendall(client, hdr, hdr_len);
	if (ret) {
		LOG_ERR("Snapshot: hdr send failed (%d)", ret);
		return ret;
	}

	ret = sendall(client, frame_data, frame_size);
	if (ret) {
		LOG_ERR("Snapshot: body send failed (%d)", ret);
		return ret;
	}

	LOG_INF("Snapshot served: %zu bytes in %lld ms",
		frame_size, k_uptime_get() - t0);
	return 0;
}

/*
 * Stream worker thread — waits for stream_sem, runs MJPEG or WS stream,
 * then closes the socket and goes back to waiting.
 */
static void stream_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("Stream thread started, waiting for clients");

	while (1) {
		k_sem_take(&stream_sem, K_FOREVER);

		int fd = stream_client_fd;
		LOG_INF("Stream thread woke: fd=%d mode=%d", fd, stream_mode);

		if (fd < 0) {
			continue;
		}

		if (stream_mode == STREAM_WS_BINARY) {
			/* Complete WS handshake on stream thread (heavier stack) */
			if (ws_handshake(fd, ws_pending_key) < 0) {
				LOG_ERR("WS handshake failed on stream thread");
				wait_for_peer_close(fd, 500);
				zsock_close(fd);
				stream_client_fd = -1;
				continue;
			}
			handle_ws_stream(fd);
		} else if (stream_mode == STREAM_SNAPSHOT) {
			handle_snapshot(fd);
		} else {
			handle_mjpeg_stream(fd);
		}

		/* Wait for client to close first, then close our side */
		wait_for_peer_close(fd, 1000);
		zsock_close(fd);

		/* Cooldown: keep stream_client_fd set so new requests get 503.
		 * This lets TCP retransmissions drain and net_bufs be freed
		 * before we accept another stream. */
		LOG_INF("Stream fd=%d closed, cooldown %ds",
			fd, STREAM_CLOSE_COOLDOWN_S);
		k_sleep(K_SECONDS(STREAM_CLOSE_COOLDOWN_S));

		stream_client_fd = -1;
		LOG_INF("Stream ready for new client");
	}
}

static void handle_client(int client, struct sockaddr_in *addr)
{
	char recv_buf[HTTP_RECV_BUF_SIZE];
	char method[8], path[64];
	ssize_t total = 0;

	/* Set send timeout to protect against slow clients */
	struct timeval tv = {
		.tv_sec = HTTP_SEND_TIMEOUT_MS / 1000,
		.tv_usec = (HTTP_SEND_TIMEOUT_MS % 1000) * 1000,
	};
	zsock_setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	/* Set recv timeout for header reading */
	struct timeval rtv = { .tv_sec = 2 };
	zsock_setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));

	LOG_INF("handle_client fd=%d", client);

	/* Read HTTP request until \r\n\r\n or buffer full */
	while (total < (ssize_t)sizeof(recv_buf) - 1) {
		ssize_t n = zsock_recv(client, recv_buf + total,
				       sizeof(recv_buf) - 1 - total, 0);
		if (n <= 0) {
			if (total == 0) {
				LOG_WRN("recv returned %zd with total=0, errno=%d", n, errno);
				zsock_close(client);
				return;
			}
			break;
		}
		total += n;
		recv_buf[total] = '\0';
		if (strstr(recv_buf, "\r\n\r\n")) {
			break;
		}
	}

	/* Reject if headers didn't fit in buffer (no \r\n\r\n found) */
	if (!strstr(recv_buf, "\r\n\r\n")) {
		LOG_WRN("Request headers too large (%zd bytes)", total);
		sendall(client, http_404, sizeof(http_404) - 1);
		zsock_close(client);
		return;
	}

	if (parse_request(recv_buf, method, sizeof(method),
			  path, sizeof(path)) < 0) {
		zsock_close(client);
		return;
	}

	LOG_INF("%s %s from %d.%d.%d.%d",
		method, path,
		addr->sin_addr.s4_addr[0], addr->sin_addr.s4_addr[1],
		addr->sin_addr.s4_addr[2], addr->sin_addr.s4_addr[3]);

	if (strcmp(method, "GET") != 0) {
		sendall(client, http_404, sizeof(http_404) - 1);
		wait_for_peer_close(client, 500);
		zsock_close(client);
		return;
	}

	if (strcmp(path, "/") == 0) {
		handle_index(client);
	} else if (strcmp(path, "/ws") == 0) {
		handle_ws_page(client);
	} else if (strcmp(path, "/stream/tcp") == 0) {
		/* Hand off to stream worker (only 1 active stream) */
		if (stream_client_fd >= 0) {
			LOG_WRN("Stream busy, rejecting");
			sendall(client, http_503, sizeof(http_503) - 1);
		} else {
			LOG_INF("Handing off stream fd=%d to stream thread", client);
			stream_mode = STREAM_MJPEG_TCP;
			stream_client_fd = client;
			k_sem_give(&stream_sem);
			return; /* Stream thread owns the fd now */
		}
	} else if (strcmp(path, "/stream/ws") == 0) {
		/* WebSocket upgrade — extract key, hand off to stream worker */
		if (stream_client_fd >= 0) {
			LOG_WRN("Stream busy, rejecting WS");
			sendall(client, http_503, sizeof(http_503) - 1);
		} else {
			LOG_INF("WS upgrade request, extracting key...");
			const char *key_val = find_header(recv_buf,
				"Sec-WebSocket-Key:");
			if (!key_val) {
				LOG_ERR("WS: missing key header");
				sendall(client, http_404, sizeof(http_404) - 1);
			} else {
				int ki = 0;

				while (*key_val && *key_val != '\r'
				       && *key_val != '\n' && ki < 24) {
					ws_pending_key[ki++] = *key_val++;
				}
				ws_pending_key[ki] = '\0';
				LOG_INF("WS key extracted (%d chars), handing to stream thread", ki);
				stream_mode = STREAM_WS_BINARY;
				stream_client_fd = client;
				k_sem_give(&stream_sem);
				return; /* Stream thread owns the fd now */
			}
		}
	} else if (strcmp(path, "/api/status") == 0) {
		handle_api_status(client, recv_buf, sizeof(recv_buf));
	} else if (strncmp(path, "/api/led/", 9) == 0) {
		handle_api_led(client, path + 9, recv_buf, sizeof(recv_buf));
	} else {
		sendall(client, http_404, sizeof(http_404) - 1);
	}

	/* Close HTTP connection: wait for client to close first to avoid
	 * server-initiated FIN which corrupts ESP32 WiFi stack. */
	wait_for_peer_close(client, 1000);
	zsock_close(client);
}

/*
 * Create a TCP listen socket on the given port.
 * Returns socket fd or -1 on error.
 */
static int create_listen_socket(int port)
{
	int sock, optval = 1;
	struct sockaddr_in addr;

	sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		LOG_ERR("Failed to create socket for port %d: %d", port, errno);
		return -1;
	}

	zsock_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
			 &optval, sizeof(optval));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (zsock_bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		LOG_ERR("Failed to bind port %d: %d", port, errno);
		zsock_close(sock);
		return -1;
	}

	if (zsock_listen(sock, HTTP_LISTEN_BACKLOG) < 0) {
		LOG_ERR("Failed to listen on port %d: %d", port, errno);
		zsock_close(sock);
		return -1;
	}

	return sock;
}

/*
 * Handle a stream-port connection: read the HTTP request header,
 * then hand off to stream thread (MJPEG or WS).
 */
static void handle_stream_client(int client, struct sockaddr_in *addr)
{
	char recv_buf[HTTP_RECV_BUF_SIZE];
	char method[8], path[64];
	ssize_t total = 0;

	struct timeval tv = {
		.tv_sec = HTTP_SEND_TIMEOUT_MS / 1000,
		.tv_usec = (HTTP_SEND_TIMEOUT_MS % 1000) * 1000,
	};
	zsock_setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	struct timeval rtv = { .tv_sec = 2 };
	zsock_setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));

	/* Read HTTP request */
	while (total < (ssize_t)sizeof(recv_buf) - 1) {
		ssize_t n = zsock_recv(client, recv_buf + total,
				       sizeof(recv_buf) - 1 - total, 0);
		if (n <= 0) {
			if (total == 0) {
				zsock_close(client);
				return;
			}
			break;
		}
		total += n;
		recv_buf[total] = '\0';
		if (strstr(recv_buf, "\r\n\r\n")) {
			break;
		}
	}

	if (parse_request(recv_buf, method, sizeof(method),
			  path, sizeof(path)) < 0) {
		zsock_close(client);
		return;
	}

	LOG_INF("STREAM %s %s from %d.%d.%d.%d",
		method, path,
		addr->sin_addr.s4_addr[0], addr->sin_addr.s4_addr[1],
		addr->sin_addr.s4_addr[2], addr->sin_addr.s4_addr[3]);

	if (stream_client_fd >= 0) {
		LOG_WRN("Stream busy, rejecting on stream port");
		sendall(client, http_503, sizeof(http_503) - 1);
		wait_for_peer_close(client, 500);
		zsock_close(client);
		return;
	}

	if (strcmp(path, "/stream/tcp") == 0) {
		LOG_INF("Stream port: handing off MJPEG fd=%d", client);
		stream_mode = STREAM_MJPEG_TCP;
		stream_client_fd = client;
		k_sem_give(&stream_sem);
	} else if (strcmp(path, "/snapshot.jpg") == 0) {
		LOG_INF("Stream port: handing off snapshot fd=%d", client);
		stream_mode = STREAM_SNAPSHOT;
		stream_client_fd = client;
		k_sem_give(&stream_sem);
	} else if (strcmp(path, "/stream/ws") == 0) {
		const char *key_val = find_header(recv_buf,
			"Sec-WebSocket-Key:");
		if (!key_val) {
			sendall(client, http_404, sizeof(http_404) - 1);
			wait_for_peer_close(client, 500);
			zsock_close(client);
			return;
		}
		int ki = 0;
		while (*key_val && *key_val != '\r' &&
		       *key_val != '\n' && ki < 24) {
			ws_pending_key[ki++] = *key_val++;
		}
		ws_pending_key[ki] = '\0';
		LOG_INF("Stream port: WS key extracted, handing off fd=%d", client);
		stream_mode = STREAM_WS_BINARY;
		stream_client_fd = client;
		k_sem_give(&stream_sem);
	} else {
		sendall(client, http_404, sizeof(http_404) - 1);
		wait_for_peer_close(client, 500);
		zsock_close(client);
	}
}

/* HTTP listener thread — polls two listen sockets (HTTP + Stream ports) */
static void http_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int http_sock, stream_sock, client;
	struct sockaddr_in client_addr;
	socklen_t client_addr_len;

	http_sock = create_listen_socket(server_port);
	if (http_sock < 0) {
		return;
	}

	stream_sock = create_listen_socket(stream_port);
	if (stream_sock < 0) {
		zsock_close(http_sock);
		return;
	}

	LOG_INF("HTTP on port %d, Stream on port %d",
		server_port, stream_port);

	struct zsock_pollfd fds[2] = {
		{ .fd = http_sock,   .events = ZSOCK_POLLIN },
		{ .fd = stream_sock, .events = ZSOCK_POLLIN },
	};

	while (1) {
		int ret = zsock_poll(fds, 2, -1); /* Block until activity */
		if (ret < 0) {
			LOG_ERR("poll error: %d", errno);
			k_msleep(100);
			continue;
		}

		/* Check HTTP port */
		if (fds[0].revents & ZSOCK_POLLIN) {
			client_addr_len = sizeof(client_addr);
			client = zsock_accept(http_sock,
					      (struct sockaddr *)&client_addr,
					      &client_addr_len);
			if (client >= 0) {
				LOG_INF("HTTP accepted fd=%d", client);
				handle_client(client, &client_addr);
			}
		}

		/* Check stream port */
		if (fds[1].revents & ZSOCK_POLLIN) {
			client_addr_len = sizeof(client_addr);
			client = zsock_accept(stream_sock,
					      (struct sockaddr *)&client_addr,
					      &client_addr_len);
			if (client >= 0) {
				LOG_INF("Stream accepted fd=%d", client);
				handle_stream_client(client, &client_addr);
			}
		}
	}
}

K_THREAD_STACK_DEFINE(http_stack, HTTP_THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(stream_stack, STREAM_THREAD_STACK_SIZE);
static struct k_thread http_thread_data;
static struct k_thread stream_thread_data;

int http_server_start(int port)
{
	server_port = port;
	stream_port = port + 1;

	k_sem_init(&stream_sem, 0, 1);

	k_thread_create(&stream_thread_data, stream_stack,
			K_THREAD_STACK_SIZEOF(stream_stack),
			stream_thread_fn, NULL, NULL, NULL,
			STREAM_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&stream_thread_data, "mjpeg_strm");

	k_thread_create(&http_thread_data, http_stack,
			K_THREAD_STACK_SIZEOF(http_stack),
			http_thread_fn, NULL, NULL, NULL,
			HTTP_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&http_thread_data, "http_srv");

	return 0;
}
