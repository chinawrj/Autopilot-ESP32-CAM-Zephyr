/*
 * Camera Init — Implementation
 *
 * Bootstrap the OV2640 camera on YD-ESP32-CAM:
 * 1. Generate XCLK via LEDC PWM on GPIO0 (~20MHz)
 * 2. De-assert PWDN on GPIO32 (active HIGH → set LOW)
 * 3. Probe OV2640 via I2C/SCCB: read PID and VER registers
 * 4. Configure OV2640 for JPEG QVGA (320x240) output
 */

#include "camera_init.h"
#include "ov2640_regs.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(cam_init, LOG_LEVEL_INF);

/* OV2640 I2C/SCCB address (7-bit) */
#define OV2640_I2C_ADDR  0x30

/* OV2640 expected ID values */
#define OV2640_PID_VAL   0x26
#define OV2640_VER_VAL   0x42

/* Camera PWDN pin: GPIO32 (gpio1 bit 0), active HIGH */
#define CAM_PWDN_NODE DT_NODELABEL(gpio1)
#define CAM_PWDN_PIN  0  /* GPIO32 = gpio1 pin 0 */

/* XCLK: LEDC channel 0, ~20MHz */
#define XCLK_PWM_NODE DT_NODELABEL(ledc0)
#define XCLK_CHANNEL  0
#define XCLK_PERIOD_NS 50  /* 20MHz = 50ns period */
#define XCLK_PULSE_NS 25   /* 50% duty */

/* I2C bus */
#define I2C_NODE DT_NODELABEL(i2c0)

static bool cam_detected;
static bool cam_configured;

static int ov2640_read_reg(const struct device *i2c, uint8_t reg,
			   uint8_t *val)
{
	return i2c_reg_read_byte(i2c, OV2640_I2C_ADDR, reg, val);
}

static int ov2640_write_reg(const struct device *i2c, uint8_t reg,
			    uint8_t val)
{
	return i2c_reg_write_byte(i2c, OV2640_I2C_ADDR, reg, val);
}

static int ov2640_write_regs(const struct device *i2c,
			     const struct ov2640_reg *regs)
{
	int ret;

	for (int i = 0; regs[i].addr || regs[i].value; i++) {
		ret = ov2640_write_reg(i2c, regs[i].addr, regs[i].value);
		if (ret < 0) {
			LOG_ERR("Reg write failed: addr=0x%02x val=0x%02x err=%d",
				regs[i].addr, regs[i].value, ret);
			return ret;
		}
	}
	return 0;
}

static int camera_start_xclk(void)
{
	const struct device *pwm = DEVICE_DT_GET(XCLK_PWM_NODE);

	if (!device_is_ready(pwm)) {
		LOG_ERR("LEDC PWM device not ready");
		return -ENODEV;
	}

	int ret = pwm_set(pwm, XCLK_CHANNEL, XCLK_PERIOD_NS, XCLK_PULSE_NS,
			  0);
	if (ret < 0) {
		LOG_ERR("Failed to start XCLK: %d", ret);
		return ret;
	}

	LOG_INF("XCLK started (20MHz on GPIO0)");
	return 0;
}

static int camera_pwdn_deassert(void)
{
	const struct device *gpio = DEVICE_DT_GET(CAM_PWDN_NODE);

	if (!device_is_ready(gpio)) {
		LOG_ERR("GPIO1 device not ready for PWDN");
		return -ENODEV;
	}

	/* GPIO32 = gpio1 pin 0, configure as output LOW (deassert PWDN) */
	int ret = gpio_pin_configure(gpio, CAM_PWDN_PIN, GPIO_OUTPUT_INACTIVE);

	if (ret < 0) {
		LOG_ERR("Failed to configure PWDN GPIO: %d", ret);
		return ret;
	}

	gpio_pin_set(gpio, CAM_PWDN_PIN, 0);
	LOG_INF("Camera PWDN deasserted (GPIO32 LOW)");
	return 0;
}

static int camera_sccb_probe(void)
{
	const struct device *i2c = DEVICE_DT_GET(I2C_NODE);
	uint8_t pid, ver;
	int ret;

	if (!device_is_ready(i2c)) {
		LOG_ERR("I2C0 device not ready");
		return -ENODEV;
	}

	/* Select sensor register bank */
	ret = ov2640_write_reg(i2c, BANK_SEL, BANK_SEL_SENSOR);
	if (ret < 0) {
		LOG_ERR("SCCB bank select failed: %d", ret);
		return ret;
	}

	/* Read Product ID (register 0x0A in sensor bank) */
	ret = ov2640_read_reg(i2c, 0x0A, &pid);
	if (ret < 0) {
		LOG_ERR("SCCB PID read failed: %d", ret);
		return ret;
	}

	/* Read Version (register 0x0B in sensor bank) */
	ret = ov2640_read_reg(i2c, 0x0B, &ver);
	if (ret < 0) {
		LOG_ERR("SCCB VER read failed: %d", ret);
		return ret;
	}

	LOG_INF("OV2640 detected: PID=0x%02x, VER=0x%02x", pid, ver);

	if (pid != OV2640_PID_VAL || ver != OV2640_VER_VAL) {
		LOG_WRN("Unexpected sensor ID (expected PID=0x26, VER=0x42)");
		return -ENODEV;
	}

	return 0;
}

static int camera_configure_jpeg_qvga(void)
{
	const struct device *i2c = DEVICE_DT_GET(I2C_NODE);
	int ret;

	LOG_INF("Configuring OV2640 for JPEG QVGA (320x240)");

	/* Software reset */
	ret = ov2640_write_reg(i2c, BANK_SEL, BANK_SEL_SENSOR);
	if (ret < 0) { return ret; }
	ret = ov2640_write_reg(i2c, COM7, COM7_SRST);
	if (ret < 0) { return ret; }
	k_msleep(300);

	/* Write default register configuration */
	ret = ov2640_write_regs(i2c, ov2640_default_regs);
	if (ret < 0) {
		LOG_ERR("Default regs write failed: %d", ret);
		return ret;
	}
	k_msleep(10);

	/* Set UXGA sensor resolution (full sensor readout) */
	ret = ov2640_write_regs(i2c, ov2640_uxga_regs);
	if (ret < 0) {
		LOG_ERR("UXGA regs write failed: %d", ret);
		return ret;
	}
	k_msleep(10);

	/* Set QVGA output window */
	ret = ov2640_write_regs(i2c, ov2640_qvga_regs);
	if (ret < 0) {
		LOG_ERR("QVGA regs write failed: %d", ret);
		return ret;
	}
	k_msleep(10);

	/* Enable JPEG compression */
	ret = ov2640_write_regs(i2c, ov2640_jpeg_regs);
	if (ret < 0) {
		LOG_ERR("JPEG regs write failed: %d", ret);
		return ret;
	}
	k_msleep(30);

	/* Set JPEG clock: no doubler, pclk_div=8 (matches esp32-camera) */
	ret = ov2640_write_reg(i2c, BANK_SEL, BANK_SEL_SENSOR);
	if (ret < 0) { return ret; }
	ret = ov2640_write_reg(i2c, CLKRC, 0x00);
	if (ret < 0) { return ret; }
	ret = ov2640_write_reg(i2c, BANK_SEL, BANK_SEL_DSP);
	if (ret < 0) { return ret; }
	ret = ov2640_write_reg(i2c, R_DVP_SP, 0x08);
	if (ret < 0) { return ret; }

	/* Set JPEG quality (lower = better quality, 0x04 is good) */
	ret = ov2640_write_reg(i2c, BANK_SEL, BANK_SEL_DSP);
	if (ret < 0) { return ret; }
	ret = ov2640_write_reg(i2c, QS, 0x04);
	if (ret < 0) { return ret; }

	/* Verify JPEG mode by reading back IMAGE_MODE register */
	{
		uint8_t img_mode = 0;

		ret = ov2640_write_reg(i2c, BANK_SEL, BANK_SEL_DSP);
		if (ret == 0) {
			ret = ov2640_read_reg(i2c, IMAGE_MODE, &img_mode);
		}
		if (ret == 0) {
			LOG_INF("IMAGE_MODE readback: 0x%02x (JPEG=%s)",
				img_mode,
				(img_mode & IMAGE_MODE_JPEG_EN) ? "ON" : "OFF");
		}
	}

	LOG_INF("OV2640 JPEG QVGA configured");
	return 0;
}

int camera_init(void)
{
	int ret;

	LOG_INF("Camera init: YD-ESP32-CAM (OV2640)");

	/* Step 1: Start XCLK (camera needs clock before SCCB works) */
	ret = camera_start_xclk();
	if (ret < 0) {
		return ret;
	}

	/* Step 2: De-assert PWDN (power up camera) */
	ret = camera_pwdn_deassert();
	if (ret < 0) {
		return ret;
	}

	/* Wait for camera to stabilize after power-up */
	k_msleep(100);

	/* Step 3: Probe camera via SCCB/I2C */
	ret = camera_sccb_probe();
	if (ret < 0) {
		LOG_ERR("Camera probe failed — check wiring/power");
		return ret;
	}

	cam_detected = true;

	/* Step 4: Configure OV2640 for JPEG QVGA */
	ret = camera_configure_jpeg_qvga();
	if (ret < 0) {
		LOG_ERR("Camera JPEG config failed: %d", ret);
		return ret;
	}

	cam_configured = true;
	LOG_INF("Camera init complete (JPEG QVGA 320x240)");
	return 0;
}

bool camera_is_detected(void)
{
	return cam_detected;
}
