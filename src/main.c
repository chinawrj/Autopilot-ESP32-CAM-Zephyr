/*
 * Autopilot ESP32-CAM — Main Application
 *
 * LED status indicator:
 *   Slow blink (1s)  = WiFi connecting
 *   Solid on         = WiFi connected
 *   Fast blink (200ms) = Error
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "wifi_manager.h"
#include "frame_source.h"
#include "http_server.h"
#include "camera_init.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define LED0_NODE DT_ALIAS(led0)
#define HTTP_PORT 80

#if !DT_NODE_HAS_STATUS_OKAY(LED0_NODE)
#error "LED0 alias not defined in devicetree overlay"
#endif

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

static int led_init(void)
{
	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("GPIO device not ready");
		return -ENODEV;
	}

	int ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

	if (ret < 0) {
		LOG_ERR("Failed to configure LED GPIO: %d", ret);
	}
	return ret;
}

static void led_set(bool on)
{
	gpio_pin_set_dt(&led, on ? 1 : 0);
}

int main(void)
{
	int ret;

	LOG_INF("Autopilot ESP32-CAM starting...");
	LOG_INF("Board: YD-ESP32-CAM (ESP32-WROVER-E-N8R8)");

	ret = led_init();
	if (ret < 0) {
		return ret;
	}

	/* Slow blink while connecting */
	LOG_INF("Connecting to WiFi...");
	for (int i = 0; i < 3; i++) {
		led_set(true);
		k_msleep(500);
		led_set(false);
		k_msleep(500);
	}

	ret = wifi_manager_init();
	if (ret == 0) {
		LOG_INF("WiFi ready — IP: %s", wifi_manager_get_ip());
		led_set(true);
	} else {
		LOG_WRN("WiFi init returned %d, running in degraded mode", ret);
	}

	/* Initialize OV2640 camera (XCLK, PWDN, SCCB probe) */
	ret = camera_init();
	if (ret < 0) {
		LOG_WRN("Camera init failed: %d — streaming test pattern", ret);
	} else {
		LOG_INF("Camera detected: OV2640");
	}

	/* Initialize frame source (test pattern for now, camera later) */
	ret = frame_source_init();
	if (ret < 0) {
		LOG_ERR("Frame source init failed: %d", ret);
	}

	/* Start HTTP server */
	if (wifi_manager_is_connected()) {
		ret = http_server_start(HTTP_PORT);
		if (ret < 0) {
			LOG_ERR("HTTP server start failed: %d", ret);
		} else {
			LOG_INF("HTTP server started on http://%s:%d/",
				wifi_manager_get_ip(), HTTP_PORT);
		}
	}

	/* Main loop: heartbeat + status monitoring */
	while (1) {
		if (wifi_manager_is_connected()) {
			led_set(true);
			k_msleep(5000);
		} else {
			led_set(true);
			k_msleep(500);
			led_set(false);
			k_msleep(500);
		}
	}

	return 0;
}
