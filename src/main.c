/*
 * Autopilot ESP32-CAM — Main Application
 *
 * LED status indicator:
 *   Slow blink (1s)  = WiFi connecting
 *   Solid on         = WiFi connected
 *   Fast blink (200ms) = Error
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "wifi_manager.h"
#include "frame_source.h"
#include "http_server.h"
#include "camera_init.h"
#include "cam_i2s_capture.h"
#include "led_control.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define HTTP_PORT 80

int main(void)
{
	int ret;

	LOG_INF("Autopilot ESP32-CAM starting...");
	LOG_INF("Board: YD-ESP32-CAM (ESP32-WROVER-E-N8R8)");

	ret = led_control_init();
	if (ret < 0) {
		return ret;
	}

	/* Slow blink while connecting */
	LOG_INF("Connecting to WiFi...");
	for (int i = 0; i < 3; i++) {
		led_control_set(true);
		k_msleep(500);
		led_control_set(false);
		k_msleep(500);
	}
	led_control_auto();

	ret = wifi_manager_init();
	if (ret == 0) {
		LOG_INF("WiFi ready — IP: %s", wifi_manager_get_ip());
		led_control_heartbeat(true);
	} else {
		LOG_WRN("WiFi init returned %d, running in degraded mode", ret);
	}

	/* Initialize OV2640 camera (XCLK, PWDN, SCCB probe, JPEG config) */
	ret = camera_init();
	if (ret < 0) {
		LOG_WRN("Camera init failed: %d — streaming test pattern", ret);
	} else {
		LOG_INF("Camera detected: OV2640 (JPEG QVGA)");

		/* Initialize I2S capture driver */
		ret = cam_i2s_init();
		if (ret < 0) {
			LOG_WRN("I2S capture init failed: %d", ret);
		}
	}

	/* Initialize frame source (uses camera if available, else test pattern) */
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
		led_control_heartbeat(wifi_manager_is_connected());
		k_msleep(wifi_manager_is_connected() ? 5000 : 1000);
	}

	return 0;
}
