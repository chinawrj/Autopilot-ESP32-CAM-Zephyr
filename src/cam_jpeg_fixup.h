/*
 * JPEG Frame Fixup — Header
 *
 * Reconstructs a valid JPEG from raw I2S DMA data.
 * Handles missing SOI, header injection from saved DQT tables,
 * EOI detection/append, and trailing garbage trimming.
 */

#ifndef CAM_JPEG_FIXUP_H
#define CAM_JPEG_FIXUP_H

#include <stddef.h>
#include <stdint.h>

#define MAX_JPEG_HEADER 700

/**
 * Extract JPEG DQT/DHT/SOF0 tables from raw DMA data and save them
 * for future header injection.
 *
 * @param data  Raw DMA capture buffer
 * @param len   Number of bytes captured
 */
void jpeg_extract_and_save_header(const uint8_t *data, size_t len);

/** Returns the saved header length (0 if not yet extracted). */
size_t jpeg_saved_header_len(void);

/**
 * Fix up a raw frame buffer into a valid JPEG image.
 *
 * Locates SOI, injects saved header if needed, trims trailing
 * garbage, and ensures EOI is present.
 *
 * @param frame_buf       Mutable frame buffer (may be shifted in-place)
 * @param frame_pos       Number of raw bytes in frame_buf
 * @param frame_buf_size  Total capacity of frame_buf
 * @param out_ptr         [out] Pointer to start of valid JPEG data
 * @param out_len         [out] Length of valid JPEG data
 * @return 0 on success, -EINVAL if no JPEG marker found
 */
int jpeg_fixup_frame(uint8_t *frame_buf, size_t frame_pos,
		     size_t frame_buf_size,
		     const uint8_t **out_ptr, size_t *out_len);

#endif /* CAM_JPEG_FIXUP_H */
