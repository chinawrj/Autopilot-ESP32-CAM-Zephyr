/*
 * I2S Camera Hardware — Internal Header
 *
 * Low-level I2S/DMA hardware management for OV2640 DVP capture.
 * All DMA/ISR state is owned internally; callers use phase-level APIs.
 */

#ifndef CAM_I2S_HW_H
#define CAM_I2S_HW_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * One-time hardware setup: GPIO matrix, DMA ring, I2S peripheral,
 * VSYNC interrupt, and I2S interrupt allocation.
 * @return 0 on success, negative errno on failure.
 */
int cam_i2s_hw_init(void);

/**
 * Prepare for a new capture cycle: reset frame buffer position,
 * clear frame_done/vsync_blanking flags, set skip count.
 * Does NOT start DMA — call cam_i2s_hw_dma_start() next.
 */
void cam_i2s_hw_prepare(int skip_frames);

/**
 * Full DMA reset + start sequence. Resets I2S RX, DMA, descriptors,
 * then starts DMA and I2S RX.
 */
void cam_i2s_hw_dma_start(void);

/** Enable ISR byte extraction into frame buffer. */
void cam_i2s_hw_extract_start(void);

/** Disable ISR byte extraction. */
void cam_i2s_hw_extract_stop(void);

/** Stop I2S RX and DMA inlink. Clears capturing flag. */
void cam_i2s_hw_stop(void);

/** Returns true when VSYNC ISR has detected blanking period. */
bool cam_i2s_hw_vsync_blanking(void);

/** Returns true when a complete frame has been captured. */
bool cam_i2s_hw_frame_done(void);

/**
 * Get the captured frame data.
 * @param buf   Output: pointer to frame buffer (PSRAM)
 * @param size  Output: number of bytes captured so far
 */
void cam_i2s_hw_get_frame(const uint8_t **buf, size_t *size);

/** Max frame buffer size (for bounds checking). */
size_t cam_i2s_hw_frame_buf_size(void);

/**
 * Log verbose first-capture diagnostics (hex dumps, markers, etc.).
 * Only meaningful on the first frame; caller decides when to call.
 */
void cam_i2s_hw_log_diagnostics(void);

#endif /* CAM_I2S_HW_H */
