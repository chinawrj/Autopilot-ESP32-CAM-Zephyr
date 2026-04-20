/*
 * HTTP API — Implementation
 *
 * HTTP response constants, request parsing utilities, API handlers
 * (status, LED, resolution), and HTML page handlers.
 */

#include "http_api.h"
#include "stream_handler.h"
#include "led_control.h"
#include "wifi_manager.h"
#include "html_pages.h"
#include "camera_init.h"
#include "cam_i2s_capture.h"

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/sys/mem_stats.h>

LOG_MODULE_REGISTER(http_api, LOG_LEVEL_INF);

/* ---- Response templates ---- */

const char http_503[] =
	"HTTP/1.1 503 Service Unavailable\r\n"
	"Content-Type: text/plain\r\n"
	"Connection: close\r\n"
	"Content-Length: 24\r\n"
	"\r\n"
	"Stream already in use.\r\n";

const char http_200_html_hdr[] =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: text/html; charset=utf-8\r\n"
	"Connection: close\r\n"
	"Content-Length: ";

const char http_404[] =
	"HTTP/1.1 404 Not Found\r\n"
	"Content-Type: text/plain\r\n"
	"Connection: close\r\n"
	"Content-Length: 9\r\n"
	"\r\n"
	"Not Found";

const char http_200_json_hdr[] =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: application/json\r\n"
	"Access-Control-Allow-Origin: *\r\n"
	"Connection: close\r\n"
	"Content-Length: ";

/* ---- Request parsing utilities ---- */

int http_parse_request(const char *buf, char *method, size_t method_len,
		       char *path, size_t path_len)
{
	const char *p = buf;
	size_t i = 0;

	while (*p && *p != ' ' && i < method_len - 1) {
		method[i++] = *p++;
	}
	method[i] = '\0';

	if (*p != ' ') {
		return -EINVAL;
	}
	p++;

	i = 0;
	while (*p && *p != ' ' && *p != '?' && i < path_len - 1) {
		path[i++] = *p++;
	}
	path[i] = '\0';

	return 0;
}

const char *http_find_header(const char *buf, const char *name)
{
	size_t nlen = strlen(name);
	const char *p = buf;

	while ((p = strchr(p, '\n')) != NULL) {
		p++;
		bool match = true;

		for (size_t i = 0; i < nlen; i++) {
			char a = p[i];
			char b = name[i];

			if (a >= 'A' && a <= 'Z') { a += 32; }
			if (b >= 'A' && b <= 'Z') { b += 32; }
			if (a != b) {
				match = false;
				break;
			}
		}
		if (match) {
			p += nlen;
			while (*p == ' ' || *p == '\t') { p++; }
			return p;
		}
	}
	return NULL;
}

/* ---- API handlers ---- */

int handle_api_status(int client, char *buf, size_t buf_size)
{
	uint32_t uptime_s = (uint32_t)(k_uptime_get() / 1000);
	uint32_t rng = sys_rand32_get();
	int temp10 = 250 + (int)(rng % 61) - 30;
	bool active = stream_is_busy();
	const char *led_str = led_control_get_state() ? "on" : "off";
	const char *led_mode = led_control_is_manual() ? "manual" : "auto";

	extern struct k_heap _system_heap;
	struct sys_memory_stats heap_stats;
	uint32_t heap_free = 0, heap_used = 0;

	if (sys_heap_runtime_stats_get(&_system_heap.heap, &heap_stats) == 0) {
		heap_free = (uint32_t)heap_stats.free_bytes;
		heap_used = (uint32_t)heap_stats.allocated_bytes;
	}

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

	char hdr[128];
	int hdr_len = snprintf(hdr, sizeof(hdr), "%s%d\r\n\r\n",
			       http_200_json_hdr, json_len);
	int ret = http_sendall(client, hdr, hdr_len);

	if (ret) { return ret; }
	return http_sendall(client, buf, json_len);
}

int handle_api_led(int client, const char *action, char *buf,
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

	if (ret) { return ret; }
	return http_sendall(client, buf, json_len);
}

int handle_api_resolution(int client, const char *action,
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

	if (stream_is_busy()) {
		LOG_INF("Stopping stream for resolution change");
		stream_force_stop();
	}

	cam_i2s_reset_warmup();

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

/* ---- Page handlers ---- */

int handle_index(int client)
{
	char len_str[16];

	snprintf(len_str, sizeof(len_str), "%zu\r\n\r\n",
		 sizeof(index_html) - 1);

	int ret = http_sendall(client, http_200_html_hdr,
			       sizeof(http_200_html_hdr) - 1);
	if (ret) { return ret; }

	ret = http_sendall(client, len_str, strlen(len_str));
	if (ret) { return ret; }

	return http_sendall(client, index_html, sizeof(index_html) - 1);
}

int handle_ws_page(int client)
{
	char len_str[16];

	snprintf(len_str, sizeof(len_str), "%zu\r\n\r\n",
		 sizeof(ws_page_html) - 1);

	int ret = http_sendall(client, http_200_html_hdr,
			       sizeof(http_200_html_hdr) - 1);
	if (ret) { return ret; }

	ret = http_sendall(client, len_str, strlen(len_str));
	if (ret) { return ret; }

	return http_sendall(client, ws_page_html, sizeof(ws_page_html) - 1);
}
