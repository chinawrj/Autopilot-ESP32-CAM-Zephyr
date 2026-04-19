/*
 * Frame Source — Header
 *
 * Abstraction layer for video frame sources.
 * Currently returns an embedded test pattern; will be replaced by
 * real camera frames when the camera driver is integrated.
 */

#ifndef FRAME_SOURCE_H
#define FRAME_SOURCE_H

#include <stddef.h>
#include <stdint.h>

/**
 * Initialize frame source.
 * @return 0 on success, negative errno on failure.
 */
int frame_source_init(void);

/**
 * Get the current JPEG frame.
 * Returns pointer to JPEG data and its size.
 * Pointer is valid until next call to frame_source_get().
 *
 * @param data  Output: pointer to JPEG data
 * @param size  Output: size of JPEG data in bytes
 * @return 0 on success, negative errno on failure.
 */
int frame_source_get(const uint8_t **data, size_t *size);

#endif /* FRAME_SOURCE_H */
