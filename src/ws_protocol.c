/*
 * WebSocket Protocol — Implementation
 *
 * RFC 6455 WebSocket handshake and binary frame encoding.
 * Contains a minimal SHA-1 used only for the handshake accept-key.
 *
 * Functions marked noinline to prevent the compiler from merging their
 * large local arrays (W[80]=320B, buf[128]) into a single mega-frame,
 * which would overflow the 3072-byte stream thread stack.
 */

#include "ws_protocol.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/base64.h>

LOG_MODULE_REGISTER(ws_proto, LOG_LEVEL_INF);

static const char ws_guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/* SHA-1 block transform — stack-local W[80] (320 bytes) */
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

/* SHA-1 full message hash — stack-local buf[128] */
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

	size_t pad_len = (len + 9 <= 64) ? 64 : 128;

	memset(buf + len + 1, 0, pad_len - len - 1);

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

int ws_handshake(int client, const char *key,
		 int (*sendall)(int, const void *, size_t))
{
	if (strlen(key) < 16) {
		LOG_ERR("WS: key too short (%zu)", strlen(key));
		return -EINVAL;
	}

	char concat[64];
	int clen = snprintf(concat, sizeof(concat), "%s%s", key, ws_guid);

	unsigned char sha1_hash[20];

	ws_sha1((const uint8_t *)concat, clen, sha1_hash);

	char accept_b64[32];
	size_t olen;
	int ret = base64_encode(accept_b64, sizeof(accept_b64), &olen,
				sha1_hash, sizeof(sha1_hash));

	if (ret) {
		LOG_ERR("WS: base64 encode failed: %d", ret);
		return -EINVAL;
	}
	accept_b64[olen] = '\0';

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

int ws_send_binary(int client, const void *data, size_t len,
		   int (*sendall)(int, const void *, size_t))
{
	uint8_t hdr[4];
	size_t hdr_len;

	hdr[0] = 0x82; /* FIN=1, opcode=binary(2) */

	if (len < 126) {
		hdr[1] = (uint8_t)len;
		hdr_len = 2;
	} else {
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
