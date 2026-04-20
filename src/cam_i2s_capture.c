/*
 * I2S Camera Capture — Implementation
 *
 * High-level capture API. Delegates hardware management to cam_i2s_hw.
 * Owns the warmup/DQT-extraction state machine and JPEG fixup integration.
 */

#include "cam_i2s_capture.h"
#include "cam_i2s_hw.h"
#include "cam_jpeg_fixup.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(cam_i2s, LOG_LEVEL_INF);

#define WARMUP_FRAMES      30
#define STREAM_SKIP_FRAMES 2
#define CAPTURE_TIMEOUT_MS 5000

static bool warmed_up;

/* One-time DQT extraction: run DMA briefly to capture JPEG header tables */
static void extract_dqt_tables(void)
{
	LOG_INF("Extracting DQT tables (pre-capture)...");
	cam_i2s_hw_prepare(999);  /* Don't trigger frame logic */
	cam_i2s_hw_extract_start();
	cam_i2s_hw_dma_start();
	k_msleep(200);  /* ~3 frames at 15fps */
	cam_i2s_hw_stop();

	const uint8_t *buf;
	size_t len;

	cam_i2s_hw_get_frame(&buf, &len);
	if (len > 0) {
		jpeg_extract_and_save_header(buf, len);
	}
}

int cam_i2s_init(void)
{
	LOG_INF("I2S camera capture init");
	return cam_i2s_hw_init();
}

int cam_i2s_capture_frame(const uint8_t **buf, size_t *size)
{
	/* One-time DQT header extraction */
	if (!warmed_up && jpeg_saved_header_len() == 0) {
		extract_dqt_tables();
	}

	int skip = warmed_up ? STREAM_SKIP_FRAMES : WARMUP_FRAMES;

	cam_i2s_hw_prepare(skip);

	if (!warmed_up) {
		LOG_INF("Capturing frame (skipping %d warm-up frames)...",
			skip);
	}

	/* Prime I2S RX for warmup (extract_active=false by default) */
	cam_i2s_hw_dma_start();

	int64_t deadline = k_uptime_get() + CAPTURE_TIMEOUT_MS;

	/* Phase 1: Wait for VSYNC blanking after warmup skips */
	while (!cam_i2s_hw_vsync_blanking() && k_uptime_get() < deadline) {
		k_yield();
	}

	if (!cam_i2s_hw_vsync_blanking()) {
		LOG_WRN("Timeout waiting for VSYNC blanking");
		cam_i2s_hw_stop();
		return -ETIMEDOUT;
	}

	/* Phase 2: Reset DMA during blanking, enable byte extraction */
	cam_i2s_hw_dma_start();
	cam_i2s_hw_extract_start();

	/* Phase 3: Wait for complete frame (next VSYNC rising edge) */
	while (!cam_i2s_hw_frame_done() && k_uptime_get() < deadline) {
		k_yield();
	}

	/* Let ISR drain remaining descriptors */
	k_busy_wait(500);
	cam_i2s_hw_stop();

	/* First-capture diagnostics */
	if (!warmed_up) {
		cam_i2s_hw_log_diagnostics();
	}

	const uint8_t *frame;
	size_t frame_len;

	cam_i2s_hw_get_frame(&frame, &frame_len);
	if (frame_len == 0) {
		LOG_WRN("Frame captured but empty");
		return -ENODATA;
	}

	warmed_up = true;

	return jpeg_fixup_frame((uint8_t *)frame, frame_len,
				cam_i2s_hw_frame_buf_size(), buf, size);
}

void cam_i2s_reset_warmup(void)
{
	cam_i2s_hw_stop();
	warmed_up = false;
	jpeg_reset_header();
	LOG_INF("I2S warmup state reset");
}
