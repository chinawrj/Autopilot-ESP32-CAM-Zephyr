/*
 * HTTP Server — Header
 *
 * Socket-based HTTP server for ESP32-CAM web interface.
 * Endpoints:
 *   GET /           → HTML index page
 *   GET /stream/tcp → MJPEG stream (multipart/x-mixed-replace)
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

/**
 * Start HTTP server on specified port.
 * Spawns a listener thread that accepts connections.
 *
 * @param port  TCP port to listen on (typically 80)
 * @return 0 on success, negative errno on failure.
 */
int http_server_start(int port);

#endif /* HTTP_SERVER_H */
