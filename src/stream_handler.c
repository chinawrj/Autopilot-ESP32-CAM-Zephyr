/*
 * Stream Handler — Implementation
 *
 * Owns the stream worker thread and all stream state.
 * Handles MJPEG (multipart/x-mixed-replace), WebSocket binary, and
 * single-frame snapshot delivery.  Only one stream client at a time.
 */

#include "stream_handler.h"
#include "ws_protocol.h"
#include "frame_source.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(stream_hdl, LOG_LEVEL_INF);

#define STREAM_SEND_TIMEOUT_S   5
#define STREAM_CLOSE_COOLDOWN_S 3
#define MJPEG_FRAME_DELAY_MS    50
#define MJPEG_BOUNDARY          "frame"
#define WS_FRAME_DELAY_MS       50

#define STREAM_THREAD_STACK_SIZE 3072
#define STREAM_THREAD_PRIORITY   8

/* ---- shared utilities (also used by http_server.c routing) ---- */

int http_sendall(int sock, const void *buf, size_t len)
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

void http_wait_for_peer_close(int fd, int timeout_ms)
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

/* ---- stream state ---- */

static struct k_sem stream_sem;
static volatile int stream_client_fd = -1;
static volatile enum stream_mode cur_mode;
static char ws_pending_key[25];

static volatile uint32_t stream_fps10;
static volatile uint32_t stream_frame_cnt;

static const char http_503[] =
	"HTTP/1.1 503 Service Unavailable\r\n"
	"Content-Type: text/plain\r\n"
	"Connection: close\r\n"
	"Content-Length: 24\r\n"
	"\r\n"
	"Stream already in use.\r\n";

/* ---- public API ---- */

int stream_try_start(int fd, enum stream_mode mode, const char *ws_key)
{
	if (stream_client_fd >= 0) {
		return -EBUSY;
	}
	if (ws_key) {
		strncpy(ws_pending_key, ws_key, sizeof(ws_pending_key) - 1);
		ws_pending_key[sizeof(ws_pending_key) - 1] = '\0';
	}
	cur_mode = mode;
	stream_client_fd = fd;
	k_sem_give(&stream_sem);
	return 0;
}

bool stream_is_busy(void)
{
	return stream_client_fd >= 0;
}

uint32_t stream_get_fps10(void)
{
	return stream_fps10;
}

uint32_t stream_get_frame_cnt(void)
{
	return stream_frame_cnt;
}

/* ---- MJPEG stream handler ---- */

static int handle_mjpeg_stream(int client)
{
	static const char mjpeg_hdr[] =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: multipart/x-mixed-replace; boundary="
		MJPEG_BOUNDARY "\r\n"
		"Cache-Control: no-store, no-cache, must-revalidate\r\n"
		"Pragma: no-cache\r\n"
		"Connection: close\r\n"
		"\r\n";

	const uint8_t *frame_data;
	size_t frame_size;
	char part_hdr[128];
	int ret;
	uint32_t frame_count = 0;
	int64_t stream_start = k_uptime_get();

	struct timeval tv = { .tv_sec = STREAM_SEND_TIMEOUT_S };

	zsock_setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	ret = http_sendall(client, mjpeg_hdr, sizeof(mjpeg_hdr) - 1);
	if (ret) {
		LOG_ERR("MJPEG hdr send failed: %d (fd=%d)", ret, client);
		return ret;
	}

	LOG_INF("MJPEG stream started, header sent OK");

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

		int hdr_len = snprintf(part_hdr, sizeof(part_hdr),
			"--" MJPEG_BOUNDARY "\r\n"
			"Content-Type: image/jpeg\r\n"
			"Content-Length: %zu\r\n"
			"\r\n", frame_size);

		int64_t send_start = k_uptime_get();

		ret = http_sendall(client, part_hdr, hdr_len);
		if (ret) {
			break;
		}
		ret = http_sendall(client, frame_data, frame_size);
		if (ret) {
			break;
		}
		ret = http_sendall(client, "\r\n", 2);
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

/* ---- WebSocket stream handler ---- */

static int handle_ws_stream(int client)
{
	const uint8_t *frame_data;
	size_t frame_size;
	int ret;
	uint32_t frame_count = 0;
	int64_t stream_start = k_uptime_get();

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

		ret = ws_send_binary(client, frame_data, frame_size,
				     http_sendall);
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

/* ---- Snapshot handler ---- */

static int handle_snapshot(int client)
{
	const uint8_t *frame_data;
	size_t frame_size;
	char hdr[256];
	int ret;
	int64_t t0 = k_uptime_get();

	struct timeval tv = { .tv_sec = STREAM_SEND_TIMEOUT_S };

	zsock_setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	ret = frame_source_get(&frame_data, &frame_size);
	if (ret || frame_size == 0) {
		LOG_ERR("Snapshot: capture failed (%d)", ret);
		http_sendall(client, http_503, sizeof(http_503) - 1);
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

	ret = http_sendall(client, hdr, hdr_len);
	if (ret) {
		return ret;
	}

	ret = http_sendall(client, frame_data, frame_size);
	if (ret) {
		return ret;
	}

	LOG_INF("Snapshot served: %zu bytes in %lld ms",
		frame_size, k_uptime_get() - t0);
	return 0;
}

/* ---- Stream worker thread ---- */

static void stream_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("Stream thread started, waiting for clients");

	while (1) {
		k_sem_take(&stream_sem, K_FOREVER);

		int fd = stream_client_fd;

		LOG_INF("Stream thread woke: fd=%d mode=%d", fd, cur_mode);

		if (fd < 0) {
			continue;
		}

		if (cur_mode == STREAM_WS_BINARY) {
			if (ws_handshake(fd, ws_pending_key,
					 http_sendall) < 0) {
				LOG_ERR("WS handshake failed");
				http_wait_for_peer_close(fd, 500);
				zsock_close(fd);
				stream_client_fd = -1;
				continue;
			}
			handle_ws_stream(fd);
		} else if (cur_mode == STREAM_SNAPSHOT) {
			handle_snapshot(fd);
		} else {
			handle_mjpeg_stream(fd);
		}

		http_wait_for_peer_close(fd, 1000);
		zsock_close(fd);

		LOG_INF("Stream fd=%d closed, cooldown %ds",
			fd, STREAM_CLOSE_COOLDOWN_S);
		k_sleep(K_SECONDS(STREAM_CLOSE_COOLDOWN_S));

		stream_client_fd = -1;
		LOG_INF("Stream ready for new client");
	}
}

K_THREAD_STACK_DEFINE(stream_stack, STREAM_THREAD_STACK_SIZE);
static struct k_thread stream_thread_data;

void stream_init(void)
{
	k_sem_init(&stream_sem, 0, 1);

	k_thread_create(&stream_thread_data, stream_stack,
			K_THREAD_STACK_SIZEOF(stream_stack),
			stream_thread_fn, NULL, NULL, NULL,
			STREAM_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&stream_thread_data, "mjpeg_strm");
}
