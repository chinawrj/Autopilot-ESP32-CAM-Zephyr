/*
 * I2S Camera Capture — Header
 *
 * Captures JPEG frames from OV2640 via ESP32 I2S0 in camera mode.
 * Uses SRAM DMA bounce buffers + copy to PSRAM.
 */

#ifndef CAM_I2S_CAPTURE_H
#define CAM_I2S_CAPTURE_H

#include <stdint.h>
#include <stddef.h>

/**
 * Initialize I2S0 in camera mode for DVP capture.
 * Must be called after camera_init() has configured the OV2640.
 * @return 0 on success, negative errno on failure.
 */
int cam_i2s_init(void);

/**
 * Capture a single JPEG frame.
 * Blocks until one complete VSYNC-delimited frame is received.
 * @param buf     Output: pointer to frame data (PSRAM, valid until next capture)
 * @param size    Output: frame size in bytes
 * @return 0 on success, negative errno on failure or timeout.
 */
int cam_i2s_capture_frame(const uint8_t **buf, size_t *size);

#endif /* CAM_I2S_CAPTURE_H */
