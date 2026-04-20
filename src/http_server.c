/*
 * HTTP Server — Implementation
 *
 * Socket-based HTTP server using Zephyr BSD sockets.
 * Architecture: listener thread accepts connections, serves static
 * pages inline, hands MJPEG/WebSocket stream to the stream_handler module.
 * Max 1 concurrent stream client (TCP or WebSocket).
 */

#include "http_server.h"
#include "stream_handler.h"
#include "led_control.h"
#include "wifi_manager.h"
#include "html_pages.h"
#include "camera_init.h"
#include "cam_i2s_capture.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/sys/mem_stats.h>

LOG_MODULE_REGISTER(http_srv, LOG_LEVEL_INF);

#define HTTP_LISTEN_BACKLOG   2
#define HTTP_RECV_BUF_SIZE    1024
#define HTTP_SEND_TIMEOUT_MS  5000
#define HTTP_THREAD_STACK_SIZE 3072
#define HTTP_THREAD_PRIORITY   7

static int server_port;
static int stream_port;

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

/* ---- request parsing utilities ---- */

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

static int handle_api_status(int client, char *buf, size_t buf_size)
{
	uint32_t uptime_s = (uint32_t)(k_uptime_get() / 1000);
	/* Simulated temperature: 25.0 ± 3.0°C (integer tenths) */
	uint32_t rng = sys_rand32_get();
	int temp10 = 250 + (int)(rng % 61) - 30; /* 220..280 → 22.0..28.0 */
	bool active = (stream_is_busy());
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
		"{\"version\":\"1.0.0\",\"fps\":%u,\"uptime\":%u,\"temp\":%d,"
		"\"led\":\"%s\",\"led_mode\":\"%s\","
		"\"stream\":%s,\"frames\":%u,"
		"\"resolution\":\"%s\","
		"\"heap_free\":%u,\"heap_used\":%u,"
		"\"rssi\":%d,\"channel\":%u,"
		"\"wifi_disconnects\":%u,\"wifi_reconnects\":%u}",
		stream_get_fps10(), uptime_s, temp10,
		led_str, led_mode,
		active ? "true" : "false",
		stream_get_frame_cnt(),
		camera_resolution_name(camera_get_resolution()),
		heap_free, heap_used,
		rssi, channel,
		wifi_dc, wifi_rc);

	/* Build HTTP header in a small temp area after the JSON */
	char hdr[128];
	int hdr_len = snprintf(hdr, sizeof(hdr), "%s%d\r\n\r\n",
			       http_200_json_hdr, json_len);
	int ret = http_sendall(client, hdr, hdr_len);

	if (ret) {
		return ret;
	}
	return http_sendall(client, buf, json_len);
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
		http_sendall(client, http_404, sizeof(http_404) - 1);
		return 0;
	}

	const char *state = led_control_get_state() ? "on" : "off";
	const char *mode = led_control_is_manual() ? "manual" : "auto";
	int json_len = snprintf(buf, buf_size,
		"{\"led\":\"%s\",\"mode\":\"%s\"}", state, mode);

	char hdr[128];
	int hdr_len = snprintf(hdr, sizeof(hdr), "%s%d\r\n\r\n",
			       http_200_json_hdr, json_len);
	int ret = http_sendall(client, hdr, hdr_len);

	if (ret) {
		return ret;
	}
	return http_sendall(client, buf, json_len);
}

static int handle_index(int client)
{
	char len_str[16];
	int ret;

	snprintf(len_str, sizeof(len_str), "%zu\r\n\r\n",
		 sizeof(index_html) - 1);

	ret = http_sendall(client, http_200_html_hdr, sizeof(http_200_html_hdr) - 1);
	if (ret) {
		return ret;
	}

	ret = http_sendall(client, len_str, strlen(len_str));
	if (ret) {
		return ret;
	}

	return http_sendall(client, index_html, sizeof(index_html) - 1);
}

static int handle_ws_page(int client)
{
	char len_str[16];
	int ret;

	snprintf(len_str, sizeof(len_str), "%zu\r\n\r\n",
		 sizeof(ws_page_html) - 1);

	ret = http_sendall(client, http_200_html_hdr, sizeof(http_200_html_hdr) - 1);
	if (ret) {
		return ret;
	}

	ret = http_sendall(client, len_str, strlen(len_str));
	if (ret) {
		return ret;
	}

	return http_sendall(client, ws_page_html, sizeof(ws_page_html) - 1);
}

static int handle_api_resolution(int client, const char *action,
				 char *buf, size_t buf_size)
{
	if (strcmp(action, "get") == 0) {
		enum camera_resolution res = camera_get_resolution();
		int w, h;

		camera_resolution_size(res, &w, &h);
		int json_len = snprintf(buf, buf_size,
			"{\"resolution\":\"%s\",\"width\":%d,\"height\":%d}",
			camera_resolution_name(res), w, h);

		char hdr[128];
		int hdr_len = snprintf(hdr, sizeof(hdr), "%s%d\r\n\r\n",
				       http_200_json_hdr, json_len);
		int ret = http_sendall(client, hdr, hdr_len);

		if (ret) { return ret; }
		return http_sendall(client, buf, json_len);
	}

	/* Parse resolution name from action */
	enum camera_resolution target;

	if (strcmp(action, "qvga") == 0) {
		target = CAM_RES_QVGA;
	} else if (strcmp(action, "vga") == 0) {
		target = CAM_RES_VGA;
	} else if (strcmp(action, "svga") == 0) {
		target = CAM_RES_SVGA;
	} else if (strcmp(action, "xga") == 0) {
		target = CAM_RES_XGA;
	} else if (strcmp(action, "sxga") == 0) {
		target = CAM_RES_SXGA;
	} else if (strcmp(action, "uxga") == 0) {
		target = CAM_RES_UXGA;
	} else {
		http_sendall(client, http_404, sizeof(http_404) - 1);
		return 0;
	}

	/* Stop any active stream before changing resolution */
	if (stream_is_busy()) {
		LOG_INF("Stopping stream for resolution change");
		stream_force_stop();
	}

	/* Reset I2S warmup state (clears JPEG header cache) */
	cam_i2s_reset_warmup();

	/* Apply new resolution */
	int ret = camera_set_resolution(target);

	if (ret < 0) {
		int json_len = snprintf(buf, buf_size,
			"{\"error\":\"failed\",\"code\":%d}", ret);
		char hdr[128];
		int hdr_len = snprintf(hdr, sizeof(hdr), "%s%d\r\n\r\n",
				       http_200_json_hdr, json_len);

		http_sendall(client, hdr, hdr_len);
		return http_sendall(client, buf, json_len);
	}

	int w, h;

	camera_resolution_size(target, &w, &h);
	int json_len = snprintf(buf, buf_size,
		"{\"resolution\":\"%s\",\"width\":%d,\"height\":%d}",
		camera_resolution_name(target), w, h);

	char hdr[128];
	int hdr_len = snprintf(hdr, sizeof(hdr), "%s%d\r\n\r\n",
			       http_200_json_hdr, json_len);

	ret = http_sendall(client, hdr, hdr_len);
	if (ret) { return ret; }
	return http_sendall(client, buf, json_len);
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
		http_sendall(client, http_404, sizeof(http_404) - 1);
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
		http_sendall(client, http_404, sizeof(http_404) - 1);
		http_wait_for_peer_close(client, 500);
		zsock_close(client);
		return;
	}

	if (strcmp(path, "/") == 0) {
		handle_index(client);
	} else if (strcmp(path, "/ws") == 0) {
		handle_ws_page(client);
	} else if (strcmp(path, "/stream/tcp") == 0) {
		if (stream_try_start(client, STREAM_MJPEG_TCP, NULL) == 0) {
			return; /* Stream thread owns the fd now */
		}
		LOG_WRN("Stream busy, rejecting");
		http_sendall(client, http_503, sizeof(http_503) - 1);
	} else if (strcmp(path, "/stream/ws") == 0) {
		const char *key_val = find_header(recv_buf,
			"Sec-WebSocket-Key:");
		if (!key_val) {
			LOG_ERR("WS: missing key header");
			http_sendall(client, http_404,
				     sizeof(http_404) - 1);
		} else {
			char ws_key[25];
			int ki = 0;

			while (*key_val && *key_val != '\r'
			       && *key_val != '\n' && ki < 24) {
				ws_key[ki++] = *key_val++;
			}
			ws_key[ki] = '\0';
			if (stream_try_start(client, STREAM_WS_BINARY,
					     ws_key) == 0) {
				return;
			}
			LOG_WRN("Stream busy, rejecting WS");
			http_sendall(client, http_503,
				     sizeof(http_503) - 1);
		}
	} else if (strcmp(path, "/api/status") == 0) {
		handle_api_status(client, recv_buf, sizeof(recv_buf));
	} else if (strncmp(path, "/api/led/", 9) == 0) {
		handle_api_led(client, path + 9, recv_buf, sizeof(recv_buf));
	} else if (strncmp(path, "/api/resolution/", 16) == 0) {
		handle_api_resolution(client, path + 16,
				      recv_buf, sizeof(recv_buf));
	} else {
		http_sendall(client, http_404, sizeof(http_404) - 1);
	}

	/* Close HTTP connection: wait for client to close first to avoid
	 * server-initiated FIN which corrupts ESP32 WiFi stack. */
	http_wait_for_peer_close(client, 1000);
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

	if (stream_is_busy()) {
		LOG_WRN("Stream busy, rejecting on stream port");
		http_sendall(client, http_503, sizeof(http_503) - 1);
		http_wait_for_peer_close(client, 500);
		zsock_close(client);
		return;
	}

	if (strcmp(path, "/stream/tcp") == 0) {
		stream_try_start(client, STREAM_MJPEG_TCP, NULL);
	} else if (strcmp(path, "/snapshot.jpg") == 0) {
		stream_try_start(client, STREAM_SNAPSHOT, NULL);
	} else if (strcmp(path, "/stream/ws") == 0) {
		const char *key_val = find_header(recv_buf,
			"Sec-WebSocket-Key:");
		if (!key_val) {
			http_sendall(client, http_404,
				     sizeof(http_404) - 1);
			http_wait_for_peer_close(client, 500);
			zsock_close(client);
			return;
		}
		char ws_key[25];
		int ki = 0;

		while (*key_val && *key_val != '\r' &&
		       *key_val != '\n' && ki < 24) {
			ws_key[ki++] = *key_val++;
		}
		ws_key[ki] = '\0';
		stream_try_start(client, STREAM_WS_BINARY, ws_key);
	} else {
		http_sendall(client, http_404, sizeof(http_404) - 1);
		http_wait_for_peer_close(client, 500);
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
static struct k_thread http_thread_data;

int http_server_start(int port)
{
	server_port = port;
	stream_port = port + 1;

	stream_init();

	k_thread_create(&http_thread_data, http_stack,
			K_THREAD_STACK_SIZEOF(http_stack),
			http_thread_fn, NULL, NULL, NULL,
			HTTP_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&http_thread_data, "http_srv");

	return 0;
}
