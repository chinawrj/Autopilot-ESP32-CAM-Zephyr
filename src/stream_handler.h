/*
 * Stream Handler — Header
 *
 * Stream worker thread for MJPEG, WebSocket, and snapshot delivery.
 * Owns stream state and telemetry; provides a clean handoff API.
 */

#ifndef STREAM_HANDLER_H
#define STREAM_HANDLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum stream_mode {
	STREAM_MJPEG_TCP,
	STREAM_WS_BINARY,
	STREAM_SNAPSHOT,
};

/**
 * Initialize the stream subsystem (semaphore + worker thread).
 * Must be called once before any stream_try_start() calls.
 */
void stream_init(void);

/**
 * Try to start a stream. Returns 0 if handoff succeeded, -EBUSY if
 * another stream is active.
 *
 * @param fd      Client socket (ownership transfers to stream thread on success)
 * @param mode    STREAM_MJPEG_TCP, STREAM_WS_BINARY, or STREAM_SNAPSHOT
 * @param ws_key  WebSocket key (only for STREAM_WS_BINARY, NULL otherwise)
 */
int stream_try_start(int fd, enum stream_mode mode, const char *ws_key);

/** Returns true if a stream client is currently active. */
bool stream_is_busy(void);

/** Returns FPS × 10 (e.g. 195 = 19.5 fps). */
uint32_t stream_get_fps10(void);

/** Returns total frames sent in current/last stream session. */
uint32_t stream_get_frame_cnt(void);

/**
 * Shared utility: send all bytes on a socket, handling partial writes.
 * Declared here because it is used by both the stream module and
 * the HTTP routing module.
 */
int http_sendall(int sock, const void *buf, size_t len);

/**
 * Wait for peer to initiate TCP close (avoids server-initiated FIN which
 * corrupts ESP32 WiFi stack on Zephyr).
 */
void http_wait_for_peer_close(int fd, int timeout_ms);

#endif /* STREAM_HANDLER_H */
