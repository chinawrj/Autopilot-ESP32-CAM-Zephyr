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
#include <zephyr/drivers/watchdog.h>

#include "wifi_manager.h"
#include "frame_source.h"
#include "http_server.h"
#include "camera_init.h"
#include "cam_i2s_capture.h"
#include "led_control.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define HTTP_PORT 80
#define WDT_TIMEOUT_MS 30000
#define APP_VERSION "1.0.0"

int main(void)
{
	int ret;

	LOG_INF("Autopilot ESP32-CAM v%s starting...", APP_VERSION);
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

	/* Start HTTP server (binds to INADDR_ANY — works before WiFi is up,
	 * clients can connect once WiFi assigns an IP) */
	ret = http_server_start(HTTP_PORT);
	if (ret < 0) {
		LOG_ERR("HTTP server start failed: %d", ret);
	} else if (wifi_manager_is_connected()) {
		LOG_INF("HTTP server started on http://%s:%d/ (stream port %d)",
			wifi_manager_get_ip(), HTTP_PORT, HTTP_PORT + 1);
	} else {
		LOG_INF("HTTP server started on port %d (waiting for WiFi)",
			HTTP_PORT);
	}

	/* Start hardware watchdog AFTER all init completes */
	const struct device *wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
	int wdt_channel_id = -1;

	if (device_is_ready(wdt)) {
		struct wdt_timeout_cfg wdt_config = {
			.window.max = WDT_TIMEOUT_MS,
			.callback = NULL,  /* System reset on timeout */
			.flags = WDT_FLAG_RESET_SOC,
		};

		wdt_channel_id = wdt_install_timeout(wdt, &wdt_config);
		if (wdt_channel_id >= 0) {
			ret = wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
			if (ret == 0) {
				LOG_INF("Watchdog started (timeout %d ms)",
					WDT_TIMEOUT_MS);
			} else {
				LOG_WRN("Watchdog setup failed: %d", ret);
				wdt_channel_id = -1;
			}
		} else {
			LOG_WRN("Watchdog install failed: %d", wdt_channel_id);
		}
	} else {
		LOG_WRN("Watchdog device not ready");
	}

	/* Main loop: heartbeat + watchdog feed + status monitoring */
	uint32_t loop_cnt = 0;
	while (1) {
		led_control_heartbeat(wifi_manager_is_connected());

		if (wdt_channel_id >= 0) {
			wdt_feed(wdt, wdt_channel_id);
		}

		/* Periodic status log every ~30s */
		if (++loop_cnt % 6 == 0) {
			int rssi = 0;
			unsigned int channel = 0;
			uint32_t disconnects = 0, reconnects = 0;

			wifi_manager_get_link_info(&rssi, &channel);
			wifi_manager_get_stats(&disconnects, &reconnects);
			LOG_INF("Heartbeat: uptime=%us WiFi=%s rssi=%d ch=%u dc=%u rc=%u",
				(uint32_t)(k_uptime_get() / 1000),
				wifi_manager_is_connected() ? "up" : "DOWN",
				rssi, channel, disconnects, reconnects);
		}

		k_msleep(wifi_manager_is_connected() ? 5000 : 1000);
	}

	return 0;
}
