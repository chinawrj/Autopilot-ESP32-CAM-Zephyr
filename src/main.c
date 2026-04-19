/*
 * Autopilot ESP32-CAM — Main Application
 *
 * Day 1: LED blink on GPIO33 to verify build + flash pipeline.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define LED0_NODE DT_ALIAS(led0)

#if !DT_NODE_HAS_STATUS_OKAY(LED0_NODE)
#error "LED0 alias not defined in devicetree overlay"
#endif

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

#define BLINK_PERIOD_MS 1000

int main(void)
{
	int ret;

	LOG_INF("Autopilot ESP32-CAM starting...");
	LOG_INF("Board: YD-ESP32-CAM (ESP32-WROVER-E-N8R8)");

	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("GPIO device not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure LED GPIO: %d", ret);
		return ret;
	}

	LOG_INF("LED blink started (GPIO33, period=%dms)", BLINK_PERIOD_MS);

	while (1) {
		gpio_pin_toggle_dt(&led);
		k_msleep(BLINK_PERIOD_MS);
	}

	return 0;
}
