/*
 * Camera Init — Implementation
 *
 * Bootstrap the OV2640 camera on YD-ESP32-CAM:
 * 1. Generate XCLK via LEDC PWM on GPIO0 (~20MHz)
 * 2. De-assert PWDN on GPIO32 (active HIGH → set LOW)
 * 3. Probe OV2640 via I2C/SCCB: read PID and VER registers
 * 4. Configure OV2640 for JPEG QVGA (320x240) output
 *
 * SCCB I/O lives in camera_sccb.c; resolution config in camera_config.c.
 */

#include "camera_init.h"
#include "camera_sccb.h"
#include "ov2640_regs.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(cam_init, LOG_LEVEL_INF);

#define OV2640_PID_VAL   0x26
#define OV2640_VER_VAL   0x42

#define CAM_PWDN_NODE DT_NODELABEL(gpio1)
#define CAM_PWDN_PIN  0

#define XCLK_PWM_NODE DT_NODELABEL(ledc0)
#define XCLK_CHANNEL  0
#define XCLK_PERIOD_NS 50
#define XCLK_PULSE_NS 25

static bool cam_detected;

/* Declared in camera_config.c */
int camera_configure_jpeg_qvga(void);

static int camera_start_xclk(void)
{
	const struct device *pwm = DEVICE_DT_GET(XCLK_PWM_NODE);

	if (!device_is_ready(pwm)) {
		LOG_ERR("LEDC PWM device not ready");
		return -ENODEV;
	}

	int ret = pwm_set(pwm, XCLK_CHANNEL, XCLK_PERIOD_NS,
			  XCLK_PULSE_NS, 0);
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

	int ret = gpio_pin_configure(gpio, CAM_PWDN_PIN,
				     GPIO_OUTPUT_INACTIVE);
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
	const struct device *i2c = camera_sccb_get_i2c();
	uint8_t pid, ver;
	int ret;

	if (!device_is_ready(i2c)) {
		LOG_ERR("I2C0 device not ready");
		return -ENODEV;
	}

	ret = ov2640_write_reg(i2c, BANK_SEL, BANK_SEL_SENSOR);
	if (ret < 0) {
		LOG_ERR("SCCB bank select failed: %d", ret);
		return ret;
	}

	ret = ov2640_read_reg(i2c, 0x0A, &pid);
	if (ret < 0) {
		LOG_ERR("SCCB PID read failed: %d", ret);
		return ret;
	}

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

int camera_init(void)
{
	int ret;

	LOG_INF("Camera init: YD-ESP32-CAM (OV2640)");

	ret = camera_start_xclk();
	if (ret < 0) {
		return ret;
	}

	ret = camera_pwdn_deassert();
	if (ret < 0) {
		return ret;
	}

	k_msleep(100);

	ret = camera_sccb_probe();
	if (ret < 0) {
		LOG_ERR("Camera probe failed — check wiring/power");
		return ret;
	}

	cam_detected = true;

	ret = camera_configure_jpeg_qvga();
	if (ret < 0) {
		LOG_ERR("Camera JPEG config failed: %d", ret);
		return ret;
	}

	LOG_INF("Camera init complete (JPEG QVGA 320x240)");
	return 0;
}

bool camera_is_detected(void)
{
	return cam_detected;
}
