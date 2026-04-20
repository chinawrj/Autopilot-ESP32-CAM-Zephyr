/*
 * Camera SCCB — Implementation
 *
 * Low-level OV2640 I2C/SCCB register read/write transport.
 */

#include "camera_sccb.h"

#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(cam_sccb, LOG_LEVEL_INF);

#define OV2640_I2C_ADDR  0x30
#define I2C_NODE         DT_NODELABEL(i2c0)

const struct device *camera_sccb_get_i2c(void)
{
	return DEVICE_DT_GET(I2C_NODE);
}

int ov2640_read_reg(const struct device *i2c, uint8_t reg, uint8_t *val)
{
	return i2c_reg_read_byte(i2c, OV2640_I2C_ADDR, reg, val);
}

int ov2640_write_reg(const struct device *i2c, uint8_t reg, uint8_t val)
{
	return i2c_reg_write_byte(i2c, OV2640_I2C_ADDR, reg, val);
}

int ov2640_write_regs(const struct device *i2c,
		      const struct ov2640_reg *regs)
{
	int ret;

	for (int i = 0; regs[i].addr || regs[i].value; i++) {
		ret = ov2640_write_reg(i2c, regs[i].addr, regs[i].value);
		if (ret < 0) {
			LOG_ERR("Reg write failed: addr=0x%02x "
				"val=0x%02x err=%d",
				regs[i].addr, regs[i].value, ret);
			return ret;
		}
	}
	return 0;
}
