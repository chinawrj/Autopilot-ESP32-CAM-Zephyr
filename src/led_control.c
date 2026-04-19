/*
 * LED Control — Implementation
 *
 * Centralized LED ownership with auto/manual modes.
 * Auto mode: main loop heartbeat (solid = connected, blink = disconnected)
 * Manual mode: API-controlled on/off
 */

#include "led_control.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led_ctrl, LOG_LEVEL_INF);

#define LED0_NODE DT_ALIAS(led0)

#if !DT_NODE_HAS_STATUS_OKAY(LED0_NODE)
#error "LED0 alias not defined in devicetree overlay"
#endif

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static volatile bool led_on;
static volatile bool manual_mode;

int led_control_init(void)
{
	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("GPIO device not ready");
		return -ENODEV;
	}

	int ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

	if (ret < 0) {
		LOG_ERR("Failed to configure LED GPIO: %d", ret);
		return ret;
	}

	led_on = false;
	manual_mode = false;
	return 0;
}

static void set_hw(bool on)
{
	gpio_pin_set_dt(&led, on ? 1 : 0);
	led_on = on;
}

void led_control_set(bool on)
{
	manual_mode = true;
	set_hw(on);
}

void led_control_toggle(void)
{
	manual_mode = true;
	set_hw(!led_on);
}

void led_control_auto(void)
{
	manual_mode = false;
}

void led_control_heartbeat(bool wifi_connected)
{
	if (manual_mode) {
		return;
	}
	/* Auto mode: solid on when connected */
	set_hw(wifi_connected);
}

bool led_control_get_state(void)
{
	return led_on;
}

bool led_control_is_manual(void)
{
	return manual_mode;
}
