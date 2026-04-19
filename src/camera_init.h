/*
 * Camera Init — Header
 *
 * OV2640 camera bootstrap: XCLK generation, PWDN control, SCCB probe.
 */

#ifndef CAMERA_INIT_H
#define CAMERA_INIT_H

#include <stdbool.h>

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

#endif /* CAMERA_INIT_H */
