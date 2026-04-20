/*
 * JPEG Frame Fixup — Implementation
 *
 * Reconstructs valid JPEG images from raw I2S DMA capture data.
 *
 * The OV2640 camera outputs a standard JPEG stream, but the I2S DMA
 * capture may miss the first bytes (SOI + header tables) due to the
 * timing gap between VSYNC and DMA restart.  This module:
 *
 * 1. Extracts and caches the JPEG header (DQT, DHT, SOF0, SOS) from
 *    a warmup capture pass.
 * 2. On each frame, locates the first valid JPEG marker, injects the
 *    saved header if the SOI/tables were lost, and ensures proper
 *    SOI/EOI framing.
 */

#include "cam_jpeg_fixup.h"

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(jpeg_fixup, LOG_LEVEL_INF);

/*
 * Saved canonical JPEG header — extracted once during warmup.
 *
 * Layout: FF D8 | DQT×2 | DHT×4 | SOF0 | SOS ≈ ~555 bytes
 * The header ends at the last byte of the SOS marker (before scan data).
 */
static uint8_t saved_jpeg_header[MAX_JPEG_HEADER];
static size_t saved_header_len;

size_t jpeg_saved_header_len(void)
{
	return saved_header_len;
}

void jpeg_reset_header(void)
{
	saved_header_len = 0;
	LOG_INF("JPEG header cache cleared");
}

/*
 * Extract the full JPEG header from continuous capture data.
 * Scans for DQT marker (start of header) then walks markers through
 * SOF0 and SOS to find the complete header.  Builds saved_jpeg_header
 * as [FF D8 | DQT×2 | DHT×4 | SOF0 | SOS].
 *
 * In scan data, FF is always byte-stuffed (FF 00), so FF DB/C4/C0/DA
 * markers cannot appear as false positives.
 */
void jpeg_extract_and_save_header(const uint8_t *data, size_t len)
{
	size_t hdr_start = 0;
	bool found_start = false;

	for (size_t i = 0; i + 4 < len; i++) {
		if (data[i] == 0xFF && data[i + 1] == 0xDB) {
			hdr_start = i;
			found_start = true;
			break;
		}
	}
	if (!found_start) {
		LOG_WRN("No DQT marker found in warmup data (%zu bytes)", len);
		return;
	}

	/* Walk markers from DQT through SOS to find header end.
	 * Each marker has: FF xx LL HH (length includes LL HH but not FF xx).
	 * SOS is the last header marker; scan data follows immediately.
	 */
	size_t pos = hdr_start;
	size_t hdr_end = 0;
	bool found_sos = false;

	while (pos + 3 < len) {
		if (data[pos] != 0xFF) {
			break;
		}
		uint8_t marker = data[pos + 1];
		uint16_t seg_len = (data[pos + 2] << 8) | data[pos + 3];

		if (marker == 0xDA) {
			hdr_end = pos + 2 + seg_len;
			found_sos = true;
			break;
		}

		pos += 2 + seg_len;
	}

	if (!found_sos) {
		LOG_WRN("SOS marker not found walking from DQT "
			"(start=%zu, pos=%zu)", hdr_start, pos);
		return;
	}

	size_t region_len = hdr_end - hdr_start;

	if (2 + region_len > MAX_JPEG_HEADER) {
		LOG_WRN("JPEG header too large: %zu bytes", 2 + region_len);
		return;
	}

	saved_jpeg_header[0] = 0xFF;
	saved_jpeg_header[1] = 0xD8;
	memcpy(&saved_jpeg_header[2], &data[hdr_start], region_len);
	saved_header_len = 2 + region_len;

	LOG_INF("Saved JPEG header: %zu bytes (DQT..SOS from %zu byte capture)",
		saved_header_len, len);
}

/*
 * Locate the start of JPEG data in the raw buffer, inject missing
 * header bytes, trim trailing garbage, and ensure SOI/EOI framing.
 */
int jpeg_fixup_frame(uint8_t *frame_buf, size_t frame_pos,
		     size_t frame_buf_size,
		     const uint8_t **out_ptr, size_t *out_len)
{
	const uint8_t *jpeg_ptr = NULL;
	size_t jpeg_len = 0;

	if (frame_pos >= 2 && frame_buf[0] == 0xFF &&
	    frame_buf[1] == 0xD8) {
		jpeg_ptr = frame_buf;
		jpeg_len = frame_pos;
	} else {
		/* SOI missing — scan for first valid JPEG marker */
		size_t jpeg_start = 0;
		bool found = false;

		for (size_t i = 0; i < frame_pos - 1 && i < 512; i++) {
			if (frame_buf[i] == 0xFF) {
				uint8_t m = frame_buf[i + 1];

				if (m >= 0xC0 && m <= 0xFE && m != 0xFF) {
					jpeg_start = i;
					found = true;
					LOG_INF("First marker FF %02X at "
						"offset %zu", m, i);
					break;
				}
			}
		}

		if (!found) {
			return -EINVAL;
		}

		uint8_t first_marker = frame_buf[jpeg_start + 1];

		if (first_marker != 0xD8 && saved_header_len > 0) {
			/* Part of the JPEG header was lost.  Find where
			 * the captured data resumes in the saved canonical
			 * header.  Match 4+ bytes to distinguish between
			 * multiple instances of the same marker type. */
			size_t inject_len = saved_header_len;
			size_t match_len = frame_pos - jpeg_start;

			if (match_len > 6) {
				match_len = 6;
			}
			for (size_t i = 2; i + match_len <= saved_header_len;
			     i++) {
				if (memcmp(&saved_jpeg_header[i],
					   &frame_buf[jpeg_start],
					   match_len) == 0) {
					inject_len = i;
					break;
				}
			}

			size_t payload_len = frame_pos - jpeg_start;

			if (jpeg_start >= inject_len) {
				memcpy(frame_buf + jpeg_start - inject_len,
				       saved_jpeg_header, inject_len);
				jpeg_ptr = frame_buf + jpeg_start - inject_len;
				jpeg_len = inject_len + payload_len;
			} else {
				memmove(frame_buf + inject_len,
					frame_buf + jpeg_start, payload_len);
				memcpy(frame_buf, saved_jpeg_header,
				       inject_len);
				jpeg_ptr = frame_buf;
				jpeg_len = inject_len + payload_len;
			}
			LOG_INF("JPEG header injected (%zu + %zu bytes, "
				"first marker FF %02X)",
				inject_len, payload_len, first_marker);
		} else if (first_marker == 0xD8) {
			jpeg_ptr = frame_buf + jpeg_start;
			jpeg_len = frame_pos - jpeg_start;
			LOG_INF("SOI found at offset %zu", jpeg_start);
		} else {
			/* No saved header — just prepend SOI */
			if (jpeg_start >= 2) {
				frame_buf[jpeg_start - 2] = 0xFF;
				frame_buf[jpeg_start - 1] = 0xD8;
				jpeg_ptr = frame_buf + jpeg_start - 2;
				jpeg_len = frame_pos - jpeg_start + 2;
			} else {
				memmove(frame_buf + 2,
					frame_buf + jpeg_start,
					frame_pos - jpeg_start);
				frame_buf[0] = 0xFF;
				frame_buf[1] = 0xD8;
				jpeg_ptr = frame_buf;
				jpeg_len = frame_pos - jpeg_start + 2;
			}
			LOG_INF("SOI prepended (no saved header, marker "
				"FF %02X at offset %zu)",
				first_marker, jpeg_start);
		}
	}

	/* Scan backward for EOI (FF D9) and trim trailing garbage */
	bool has_eoi = false;

	for (size_t i = jpeg_len; i >= 2; i--) {
		if (jpeg_ptr[i - 2] == 0xFF && jpeg_ptr[i - 1] == 0xD9) {
			jpeg_len = i;
			has_eoi = true;
			break;
		}
	}

	if (!has_eoi) {
		uint8_t *end = (uint8_t *)jpeg_ptr + jpeg_len;

		if (end + 2 <= frame_buf + frame_buf_size) {
			end[0] = 0xFF;
			end[1] = 0xD9;
			jpeg_len += 2;
			LOG_INF("EOI appended");
		}
	}

	LOG_INF("JPEG frame: %zu bytes (EOI=%s)", jpeg_len,
		has_eoi ? "found" : "appended");

	*out_ptr = jpeg_ptr;
	*out_len = jpeg_len;
	return 0;
}
