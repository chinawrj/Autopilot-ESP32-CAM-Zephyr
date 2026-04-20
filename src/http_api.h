/*
 * HTTP API — Internal Header
 *
 * Shared declarations for HTTP response helpers and API/page handlers.
 */

#ifndef HTTP_API_H
#define HTTP_API_H

#include <stddef.h>

/* HTTP response templates */
extern const char http_503[];
extern const char http_200_html_hdr[];
extern const char http_404[];
extern const char http_200_json_hdr[];

/**
 * Parse HTTP request line: extract method and path.
 * Strips query strings from path.
 */
int http_parse_request(const char *buf, char *method, size_t method_len,
		       char *path, size_t path_len);

/** Case-insensitive HTTP header search. Returns value pointer or NULL. */
const char *http_find_header(const char *buf, const char *name);

/* API handlers — each sends a complete HTTP response to client */
int handle_api_status(int client, char *buf, size_t buf_size);
int handle_api_led(int client, const char *action, char *buf,
		   size_t buf_size);
int handle_api_resolution(int client, const char *action,
			  char *buf, size_t buf_size);

/* Page handlers */
int handle_index(int client);
int handle_ws_page(int client);

#endif /* HTTP_API_H */
