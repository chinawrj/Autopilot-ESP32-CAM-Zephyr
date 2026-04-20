/*
 * Camera Config — Implementation
 *
 * OV2640 JPEG configuration and runtime resolution switching.
 */

#include "camera_init.h"
#include "camera_sccb.h"
#include "ov2640_regs.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(cam_cfg, LOG_LEVEL_INF);

static enum camera_resolution current_res = CAM_RES_QVGA;

struct res_info {
	const char *name;
	int width;
	int height;
	const struct ov2640_reg *regs;
	uint8_t qs;
};

static const struct res_info res_table[CAM_RES_COUNT] = {
	[CAM_RES_QVGA] = { "QVGA", 320,  240,  ov2640_qvga_regs, 0x04 },
	[CAM_RES_VGA]  = { "VGA",  640,  480,  ov2640_vga_regs,  0x06 },
	[CAM_RES_SVGA] = { "SVGA", 800,  600,  ov2640_svga_regs, 0x08 },
	[CAM_RES_XGA]  = { "XGA",  1024, 768,  ov2640_xga_regs,  0x0A },
	[CAM_RES_SXGA] = { "SXGA", 1280, 1024, ov2640_sxga_regs, 0x0C },
	[CAM_RES_UXGA] = { "UXGA", 1600, 1200, ov2640_uxga_out_regs, 0x0E },
};

enum camera_resolution camera_get_resolution(void)
{
	return current_res;
}

const char *camera_resolution_name(enum camera_resolution res)
{
	if (res >= CAM_RES_COUNT) {
		return "???";
	}
	return res_table[res].name;
}

void camera_resolution_size(enum camera_resolution res,
			    int *width, int *height)
{
	if (res >= CAM_RES_COUNT) {
		*width = 0;
		*height = 0;
		return;
	}
	*width = res_table[res].width;
	*height = res_table[res].height;
}

/** Apply JPEG clock + quality + AEC/AGC + brightness settings. */
static int camera_apply_jpeg_settings(const struct device *i2c,
				      uint8_t qs)
{
	int ret;

	/* JPEG clock: no doubler, pclk_div=8 */
	ret = ov2640_write_reg(i2c, BANK_SEL, BANK_SEL_SENSOR);
	if (ret < 0) { return ret; }
	ret = ov2640_write_reg(i2c, CLKRC, 0x00);
	if (ret < 0) { return ret; }
	ret = ov2640_write_reg(i2c, BANK_SEL, BANK_SEL_DSP);
	if (ret < 0) { return ret; }
	ret = ov2640_write_reg(i2c, R_DVP_SP, 0x08);
	if (ret < 0) { return ret; }

	/* JPEG quality */
	ret = ov2640_write_reg(i2c, BANK_SEL, BANK_SEL_DSP);
	if (ret < 0) { return ret; }
	ret = ov2640_write_reg(i2c, QS, qs);
	if (ret < 0) { return ret; }

	return 0;
}

/** Enable AEC/AGC and set brightness/contrast boost. */
static int camera_apply_exposure_settings(const struct device *i2c)
{
	int ret;

	/* Re-enable AEC/AGC with gain ceiling 32x */
	ret = ov2640_write_reg(i2c, BANK_SEL, BANK_SEL_SENSOR);
	if (ret < 0) { return ret; }
	ret = ov2640_write_reg(i2c, COM8,
		COM8_DEFAULT | COM8_BNDF_EN | COM8_AGC_EN | COM8_AEC_EN);
	if (ret < 0) { return ret; }
	ret = ov2640_write_reg(i2c, COM9, 0x08 | (0x04 << 4));
	if (ret < 0) { return ret; }

	/* DSP brightness/contrast boost */
	ret = ov2640_write_reg(i2c, BANK_SEL, BANK_SEL_DSP);
	if (ret < 0) { return ret; }
	ret = ov2640_write_reg(i2c, 0x9B, 0x20);
	if (ret < 0) { return ret; }
	ret = ov2640_write_reg(i2c, 0x9C, 0x28);
	if (ret < 0) { return ret; }

	return 0;
}

/** Log AEC/AGC diagnostic registers. */
static void camera_log_diagnostics(const struct device *i2c)
{
	uint8_t val;

	ov2640_write_reg(i2c, BANK_SEL, BANK_SEL_SENSOR);
	if (ov2640_read_reg(i2c, COM8, &val) == 0) {
		LOG_INF("COM8=0x%02x (AEC=%s AGC=%s)", val,
			(val & COM8_AEC_EN) ? "on" : "off",
			(val & COM8_AGC_EN) ? "on" : "off");
	}
	if (ov2640_read_reg(i2c, COM9, &val) == 0) {
		LOG_INF("COM9=0x%02x (gain_ceil=%dx)",
			val, 2 << ((val >> 4) & 0x07));
	}
	if (ov2640_read_reg(i2c, 0x00, &val) == 0) {
		LOG_INF("GAIN=0x%02x", val);
	}

	uint8_t aec_lo = 0, aec_hi = 0;

	ov2640_read_reg(i2c, 0x10, &aec_lo);
	ov2640_read_reg(i2c, 0x45, &aec_hi);
	LOG_INF("AEC=0x%02x AECH=0x%02x (exposure=%u)",
		aec_lo, aec_hi, ((uint16_t)aec_hi << 8) | aec_lo);

	/* Verify JPEG mode */
	uint8_t img_mode = 0;

	if (ov2640_write_reg(i2c, BANK_SEL, BANK_SEL_DSP) == 0 &&
	    ov2640_read_reg(i2c, IMAGE_MODE, &img_mode) == 0) {
		LOG_INF("IMAGE_MODE: 0x%02x (JPEG=%s)", img_mode,
			(img_mode & IMAGE_MODE_JPEG_EN) ? "ON" : "OFF");
	}
}

int camera_configure_jpeg_qvga(void)
{
	const struct device *i2c = camera_sccb_get_i2c();
	int ret;

	LOG_INF("Configuring OV2640 for JPEG QVGA (320x240)");

	/* Software reset */
	ret = ov2640_write_reg(i2c, BANK_SEL, BANK_SEL_SENSOR);
	if (ret < 0) { return ret; }
	ret = ov2640_write_reg(i2c, COM7, COM7_SRST);
	if (ret < 0) { return ret; }
	k_msleep(300);

	/* Default → UXGA sensor → QVGA output → JPEG */
	ret = ov2640_write_regs(i2c, ov2640_default_regs);
	if (ret < 0) { return ret; }
	k_msleep(10);

	ret = ov2640_write_regs(i2c, ov2640_uxga_regs);
	if (ret < 0) { return ret; }
	k_msleep(10);

	ret = ov2640_write_regs(i2c, ov2640_qvga_regs);
	if (ret < 0) { return ret; }
	k_msleep(10);

	ret = ov2640_write_regs(i2c, ov2640_jpeg_regs);
	if (ret < 0) { return ret; }
	k_msleep(30);

	/* JPEG clock + quality */
	ret = camera_apply_jpeg_settings(i2c, 0x04);
	if (ret < 0) { return ret; }

	/* AEC/AGC + brightness */
	ret = camera_apply_exposure_settings(i2c);
	if (ret < 0) { return ret; }

	camera_log_diagnostics(i2c);

	LOG_INF("OV2640 JPEG QVGA configured");
	return 0;
}

int camera_set_resolution(enum camera_resolution res)
{
	const struct device *i2c = camera_sccb_get_i2c();
	int ret;

	if (res >= CAM_RES_COUNT) {
		return -EINVAL;
	}

	const struct res_info *ri = &res_table[res];

	LOG_INF("Setting resolution to %s (%dx%d, QS=0x%02x)",
		ri->name, ri->width, ri->height, ri->qs);

	/* Write output window registers */
	ret = ov2640_write_regs(i2c, ri->regs);
	if (ret < 0) {
		LOG_ERR("Resolution regs write failed: %d", ret);
		return ret;
	}
	k_msleep(10);

	/* Re-apply JPEG mode */
	ret = ov2640_write_regs(i2c, ov2640_jpeg_regs);
	if (ret < 0) {
		LOG_ERR("JPEG regs write failed: %d", ret);
		return ret;
	}
	k_msleep(30);

	/* Clock + quality for this resolution */
	ret = camera_apply_jpeg_settings(i2c, ri->qs);
	if (ret < 0) { return ret; }

	current_res = res;
	LOG_INF("Resolution set to %s", ri->name);
	return 0;
}
