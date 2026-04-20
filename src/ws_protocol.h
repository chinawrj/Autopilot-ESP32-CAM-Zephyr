/*
 * WebSocket Protocol — Header
 *
 * RFC 6455 WebSocket handshake and frame encoding utilities.
 * SHA-1 implementation for handshake accept-key computation.
 */

#ifndef WS_PROTOCOL_H
#define WS_PROTOCOL_H

#include <stddef.h>

/**
 * Perform WebSocket upgrade handshake (101 Switching Protocols).
 * Computes SHA-1(key + GUID), Base64-encodes it, sends 101 response.
 *
 * @param client  Connected socket fd
 * @param key     Sec-WebSocket-Key from client (24-char base64)
 * @param sendall Function pointer: int sendall(int sock, const void *buf, size_t len)
 * @return 0 on success, negative errno on error.
 */
int ws_handshake(int client, const char *key,
		 int (*sendall)(int, const void *, size_t));

/**
 * Send a WebSocket binary frame (FIN=1, opcode=0x2).
 * Handles extended payload length for frames up to 65535 bytes.
 *
 * @param client  Connected socket fd
 * @param data    Payload data
 * @param len     Payload length
 * @param sendall Function pointer for reliable send
 * @return 0 on success, negative errno on error.
 */
int ws_send_binary(int client, const void *data, size_t len,
		   int (*sendall)(int, const void *, size_t));

#endif /* WS_PROTOCOL_H */
