/*
 * Frame Source — Implementation
 *
 * Returns JPEG frames from the OV2640 camera via I2S capture.
 * Falls back to an embedded test pattern if camera is unavailable.
 */

#include "frame_source.h"
#include "cam_i2s_capture.h"
#include "test_pattern_jpg.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(frame_src, LOG_LEVEL_INF);

static bool camera_available;

int frame_source_init(void)
{
	/* Probe camera by capturing a test frame */
	const uint8_t *frame;
	size_t size;
	int ret = cam_i2s_capture_frame(&frame, &size);

	if (ret == 0 && size > 0) {
		camera_available = true;
		LOG_INF("Frame source: OV2640 camera (test frame %zu bytes)",
			size);
	} else {
		camera_available = false;
		LOG_INF("Frame source: test pattern (%u bytes, 160x120)",
			TEST_PATTERN_JPG_SIZE);
	}
	return 0;
}

int frame_source_get(const uint8_t **data, size_t *size)
{
	if (data == NULL || size == NULL) {
		return -EINVAL;
	}

	if (camera_available) {
		int ret = cam_i2s_capture_frame(data, size);

		if (ret == 0) {
			return 0;
		}
		LOG_WRN("Camera capture failed (%d), using test pattern", ret);
	}

	*data = test_pattern_jpg;
	*size = TEST_PATTERN_JPG_SIZE;
	return 0;
}
