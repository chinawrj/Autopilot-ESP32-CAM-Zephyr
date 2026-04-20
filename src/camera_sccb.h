/*
 * Camera SCCB — Header
 *
 * Low-level OV2640 I2C/SCCB register access.
 */

#ifndef CAMERA_SCCB_H
#define CAMERA_SCCB_H

#include <zephyr/device.h>
#include "ov2640_regs.h"

/** Get the I2C device used for OV2640 SCCB communication. */
const struct device *camera_sccb_get_i2c(void);

/** Read a single OV2640 register. */
int ov2640_read_reg(const struct device *i2c, uint8_t reg, uint8_t *val);

/** Write a single OV2640 register. */
int ov2640_write_reg(const struct device *i2c, uint8_t reg, uint8_t val);

/** Write an array of OV2640 registers (terminated by {0,0}). */
int ov2640_write_regs(const struct device *i2c,
		      const struct ov2640_reg *regs);

#endif /* CAMERA_SCCB_H */
