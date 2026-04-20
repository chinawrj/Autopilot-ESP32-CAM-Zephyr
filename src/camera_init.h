/*
 * Camera Init — Header
 *
 * OV2640 camera bootstrap: XCLK generation, PWDN control, SCCB probe.
 */

#ifndef CAMERA_INIT_H
#define CAMERA_INIT_H

#include <stdbool.h>

/** Supported camera resolutions */
enum camera_resolution {
	CAM_RES_QVGA = 0,  /* 320x240 */
	CAM_RES_VGA,        /* 640x480 */
	CAM_RES_SVGA,       /* 800x600 */
	CAM_RES_XGA,        /* 1024x768 */
	CAM_RES_SXGA,       /* 1280x1024 */
	CAM_RES_UXGA,       /* 1600x1200 */
	CAM_RES_COUNT
};

/**
 * Initialize camera hardware (XCLK, PWDN) and verify OV2640 SCCB.
 * Called from main after system init.
 * @return 0 on success, negative errno on failure.
 */
int camera_init(void);

/**
 * Check if camera was detected during init.
 * @return true if OV2640 was successfully detected.
 */
bool camera_is_detected(void);

/**
 * Change camera resolution at runtime.
 * Caller must ensure no stream/capture is active.
 * Invalidates JPEG header cache and forces a re-warmup.
 * @param res  Target resolution
 * @return 0 on success, negative errno on failure.
 */
int camera_set_resolution(enum camera_resolution res);

/**
 * Get the current camera resolution.
 */
enum camera_resolution camera_get_resolution(void);

/**
 * Get resolution name string (e.g. "QVGA", "VGA").
 */
const char *camera_resolution_name(enum camera_resolution res);

/**
 * Get resolution width and height.
 */
void camera_resolution_size(enum camera_resolution res,
			    int *width, int *height);

#endif /* CAMERA_INIT_H */
