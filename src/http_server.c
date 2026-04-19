/*
 * HTTP Server — Implementation
 *
 * Socket-based HTTP server using Zephyr BSD sockets.
 * Architecture: 1 listener thread + 1 streaming worker.
 * Max 1 concurrent MJPEG stream client.
 */

#include "http_server.h"
#include "frame_source.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(http_srv, LOG_LEVEL_INF);

#define HTTP_LISTEN_BACKLOG  2
#define HTTP_RECV_BUF_SIZE   512
#define HTTP_SEND_TIMEOUT_MS 5000
#define MJPEG_FRAME_DELAY_MS 50    /* Minimal delay; capture itself takes time */
#define MJPEG_BOUNDARY       "frame"

#define HTTP_THREAD_STACK_SIZE 4096
#define HTTP_THREAD_PRIORITY   7

static int server_port;

/* Index HTML page — embedded in firmware */
static const char index_html[] =
	"<!DOCTYPE html><html><head>"
	"<meta charset=\"utf-8\">"
	"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
	"<title>ESP32-CAM</title>"
	"<style>"
	"body{font-family:sans-serif;text-align:center;background:#1a1a2e;color:#eee;margin:0;padding:20px}"
	"h1{color:#0ff}img{max-width:100%;border:2px solid #0ff;border-radius:8px}"
	"a{color:#0ff;text-decoration:none;font-size:1.2em}"
	".status{background:#16213e;padding:10px;border-radius:8px;margin:10px auto;max-width:400px}"
	"</style></head><body>"
	"<h1>&#x1F4F7; ESP32-CAM</h1>"
	"<div class=\"status\">Zephyr RTOS | YD-ESP32-CAM</div>"
	"<h2>Live Stream</h2>"
	"<img src=\"/stream/tcp\" alt=\"MJPEG Stream\">"
	"<br><br>"
	"<a href=\"/stream/tcp\">Direct Stream Link</a>"
	"</body></html>";

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

/* Parse HTTP method and path from request line */
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

static int handle_mjpeg_stream(int client)
{
	char part_hdr[128];
	const uint8_t *frame_data;
	size_t frame_size;
	int ret;

	/* Send MJPEG response header */
	ret = sendall(client, http_mjpeg_hdr, sizeof(http_mjpeg_hdr) - 1);
	if (ret) {
		return ret;
	}

	LOG_INF("MJPEG stream started");

	/* Stream frames until client disconnects */
	while (1) {
		ret = frame_source_get(&frame_data, &frame_size);
		if (ret) {
			LOG_ERR("Failed to get frame: %d", ret);
			break;
		}

		/* MJPEG part header */
		int hdr_len = snprintf(part_hdr, sizeof(part_hdr),
			"--" MJPEG_BOUNDARY "\r\n"
			"Content-Type: image/jpeg\r\n"
			"Content-Length: %zu\r\n"
			"\r\n", frame_size);

		ret = sendall(client, part_hdr, hdr_len);
		if (ret) {
			break;
		}

		ret = sendall(client, frame_data, frame_size);
		if (ret) {
			break;
		}

		ret = sendall(client, "\r\n", 2);
		if (ret) {
			break;
		}

		k_msleep(MJPEG_FRAME_DELAY_MS);
	}

	LOG_INF("MJPEG stream ended");
	return 0;
}

static void handle_client(int client, struct sockaddr_in *addr)
{
	char recv_buf[HTTP_RECV_BUF_SIZE];
	char method[8], path[64];
	ssize_t len;

	/* Set send timeout to protect against slow clients */
	struct timeval tv = {
		.tv_sec = HTTP_SEND_TIMEOUT_MS / 1000,
		.tv_usec = (HTTP_SEND_TIMEOUT_MS % 1000) * 1000,
	};
	zsock_setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	/* Receive HTTP request */
	len = zsock_recv(client, recv_buf, sizeof(recv_buf) - 1, 0);
	if (len <= 0) {
		goto done;
	}
	recv_buf[len] = '\0';

	if (parse_request(recv_buf, method, sizeof(method),
			  path, sizeof(path)) < 0) {
		goto done;
	}

	LOG_INF("%s %s from %d.%d.%d.%d",
		method, path,
		addr->sin_addr.s4_addr[0], addr->sin_addr.s4_addr[1],
		addr->sin_addr.s4_addr[2], addr->sin_addr.s4_addr[3]);

	if (strcmp(method, "GET") != 0) {
		sendall(client, http_404, sizeof(http_404) - 1);
		goto done;
	}

	if (strcmp(path, "/") == 0) {
		handle_index(client);
	} else if (strcmp(path, "/stream/tcp") == 0) {
		handle_mjpeg_stream(client);
	} else {
		sendall(client, http_404, sizeof(http_404) - 1);
	}

done:
	zsock_close(client);
}

/* HTTP listener thread */
static void http_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int sock, client;
	struct sockaddr_in addr, client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	int optval = 1;

	sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		LOG_ERR("Failed to create socket: %d", errno);
		return;
	}

	zsock_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
			 &optval, sizeof(optval));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(server_port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (zsock_bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		LOG_ERR("Failed to bind port %d: %d", server_port, errno);
		zsock_close(sock);
		return;
	}

	if (zsock_listen(sock, HTTP_LISTEN_BACKLOG) < 0) {
		LOG_ERR("Failed to listen: %d", errno);
		zsock_close(sock);
		return;
	}

	LOG_INF("HTTP server listening on port %d", server_port);

	while (1) {
		client = zsock_accept(sock, (struct sockaddr *)&client_addr,
				      &client_addr_len);
		if (client < 0) {
			LOG_ERR("Accept failed: %d", errno);
			k_msleep(100);
			continue;
		}

		handle_client(client, &client_addr);
	}
}

K_THREAD_STACK_DEFINE(http_stack, HTTP_THREAD_STACK_SIZE);
static struct k_thread http_thread_data;

int http_server_start(int port)
{
	server_port = port;

	k_thread_create(&http_thread_data, http_stack,
			K_THREAD_STACK_SIZEOF(http_stack),
			http_thread_fn, NULL, NULL, NULL,
			HTTP_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&http_thread_data, "http_srv");

	return 0;
}
