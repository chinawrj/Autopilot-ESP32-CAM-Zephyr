/*
 * Frame Source — Implementation
 *
 * Returns an embedded JPEG test pattern.
 * Will be replaced by real camera capture when driver is ready.
 */

#include "frame_source.h"
#include "test_pattern_jpg.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(frame_src, LOG_LEVEL_INF);

int frame_source_init(void)
{
	LOG_INF("Frame source: test pattern (%u bytes, 160x120)",
		TEST_PATTERN_JPG_SIZE);
	return 0;
}

int frame_source_get(const uint8_t **data, size_t *size)
{
	if (data == NULL || size == NULL) {
		return -EINVAL;
	}

	*data = test_pattern_jpg;
	*size = TEST_PATTERN_JPG_SIZE;
	return 0;
}
