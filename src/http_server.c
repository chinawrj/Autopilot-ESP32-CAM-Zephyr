/*
 * HTTP Server — Implementation
 *
 * Socket-based HTTP server using Zephyr BSD sockets.
 * Architecture: listener thread accepts connections, serves static
 * pages inline, hands MJPEG/WebSocket stream to the stream_handler module.
 * Max 1 concurrent stream client (TCP or WebSocket).
 */

#include "http_server.h"
#include "http_api.h"
#include "stream_handler.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(http_srv, LOG_LEVEL_INF);

#define HTTP_LISTEN_BACKLOG   2
#define HTTP_RECV_BUF_SIZE    1024
#define HTTP_SEND_TIMEOUT_MS  5000
#define HTTP_THREAD_STACK_SIZE 3072
#define HTTP_THREAD_PRIORITY   7

static int server_port;
static int stream_port;

/**
 * Read HTTP request headers into buffer until \r\n\r\n.
 * Sets send/recv timeouts on the socket.
 * @return bytes read, or -1 on error (caller should close socket).
 */
static ssize_t http_read_request(int client, char *buf, size_t buf_size)
{
	struct timeval tv = {
		.tv_sec = HTTP_SEND_TIMEOUT_MS / 1000,
		.tv_usec = (HTTP_SEND_TIMEOUT_MS % 1000) * 1000,
	};

	zsock_setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	struct timeval rtv = { .tv_sec = 2 };

	zsock_setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));

	ssize_t total = 0;

	while (total < (ssize_t)buf_size - 1) {
		ssize_t n = zsock_recv(client, buf + total,
				       buf_size - 1 - total, 0);
		if (n <= 0) {
			if (total == 0) {
				return -1;
			}
			break;
		}
		total += n;
		buf[total] = '\0';
		if (strstr(buf, "\r\n\r\n")) {
			break;
		}
	}

	return total;
}

/** Extract WebSocket key from headers into ws_key buffer. */
static bool extract_ws_key(const char *recv_buf, char *ws_key,
			   size_t ws_key_size)
{
	const char *key_val = http_find_header(recv_buf,
		"Sec-WebSocket-Key:");
	if (!key_val) {
		return false;
	}

	int ki = 0;

	while (*key_val && *key_val != '\r' &&
	       *key_val != '\n' && ki < (int)ws_key_size - 1) {
		ws_key[ki++] = *key_val++;
	}
	ws_key[ki] = '\0';
	return true;
}

static void handle_client(int client, struct sockaddr_in *addr)
{
	char recv_buf[HTTP_RECV_BUF_SIZE];
	char method[8], path[64];

	LOG_INF("handle_client fd=%d", client);

	ssize_t total = http_read_request(client, recv_buf,
					  sizeof(recv_buf));
	if (total < 0) {
		LOG_WRN("recv failed on fd=%d", client);
		zsock_close(client);
		return;
	}

	if (!strstr(recv_buf, "\r\n\r\n")) {
		LOG_WRN("Request headers too large (%zd bytes)", total);
		http_sendall(client, http_404, strlen(http_404));
		zsock_close(client);
		return;
	}

	if (http_parse_request(recv_buf, method, sizeof(method),
			       path, sizeof(path)) < 0) {
		zsock_close(client);
		return;
	}

	LOG_INF("%s %s from %d.%d.%d.%d",
		method, path,
		addr->sin_addr.s4_addr[0], addr->sin_addr.s4_addr[1],
		addr->sin_addr.s4_addr[2], addr->sin_addr.s4_addr[3]);

	if (strcmp(method, "GET") != 0) {
		http_sendall(client, http_404, strlen(http_404));
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
			return;
		}
		LOG_WRN("Stream busy, rejecting");
		http_sendall(client, http_503, strlen(http_503));
	} else if (strcmp(path, "/stream/ws") == 0) {
		char ws_key[25];

		if (!extract_ws_key(recv_buf, ws_key, sizeof(ws_key))) {
			LOG_ERR("WS: missing key header");
			http_sendall(client, http_404,
				     strlen(http_404));
		} else if (stream_try_start(client, STREAM_WS_BINARY,
					    ws_key) == 0) {
			return;
		} else {
			LOG_WRN("Stream busy, rejecting WS");
			http_sendall(client, http_503,
				     strlen(http_503));
		}
	} else if (strcmp(path, "/api/status") == 0) {
		handle_api_status(client, recv_buf, sizeof(recv_buf));
	} else if (strncmp(path, "/api/led/", 9) == 0) {
		handle_api_led(client, path + 9, recv_buf, sizeof(recv_buf));
	} else if (strncmp(path, "/api/resolution/", 16) == 0) {
		handle_api_resolution(client, path + 16,
				      recv_buf, sizeof(recv_buf));
	} else {
		http_sendall(client, http_404, strlen(http_404));
	}

	http_wait_for_peer_close(client, 1000);
	zsock_close(client);
}

static int create_listen_socket(int port)
{
	int sock, optval = 1;
	struct sockaddr_in addr;

	sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		LOG_ERR("Failed to create socket for port %d: %d",
			port, errno);
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

static void handle_stream_client(int client, struct sockaddr_in *addr)
{
	char recv_buf[HTTP_RECV_BUF_SIZE];
	char method[8], path[64];

	ssize_t total = http_read_request(client, recv_buf,
					  sizeof(recv_buf));
	if (total < 0) {
		zsock_close(client);
		return;
	}

	if (http_parse_request(recv_buf, method, sizeof(method),
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
		http_sendall(client, http_503, strlen(http_503));
		http_wait_for_peer_close(client, 500);
		zsock_close(client);
		return;
	}

	if (strcmp(path, "/stream/tcp") == 0) {
		stream_try_start(client, STREAM_MJPEG_TCP, NULL);
	} else if (strcmp(path, "/snapshot.jpg") == 0) {
		stream_try_start(client, STREAM_SNAPSHOT, NULL);
	} else if (strcmp(path, "/stream/ws") == 0) {
		char ws_key[25];

		if (!extract_ws_key(recv_buf, ws_key, sizeof(ws_key))) {
			http_sendall(client, http_404,
				     strlen(http_404));
			http_wait_for_peer_close(client, 500);
			zsock_close(client);
			return;
		}
		stream_try_start(client, STREAM_WS_BINARY, ws_key);
	} else {
		http_sendall(client, http_404, strlen(http_404));
		http_wait_for_peer_close(client, 500);
		zsock_close(client);
	}
}

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
		int ret = zsock_poll(fds, 2, -1);

		if (ret < 0) {
			LOG_ERR("poll error: %d", errno);
			k_msleep(100);
			continue;
		}

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
