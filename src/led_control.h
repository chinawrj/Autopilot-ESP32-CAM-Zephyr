/*
 * LED Control — Header
 *
 * Centralized LED ownership: supports "auto" mode (heartbeat blink)
 * and "manual" mode (API-controlled on/off).
 */

#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include <stdbool.h>

/**
 * Initialize LED GPIO. Must be called before any other led_control function.
 * @return 0 on success, negative errno on failure.
 */
int led_control_init(void);

/**
 * Set LED state directly. Switches to manual mode.
 * @param on  true = LED on, false = LED off
 */
void led_control_set(bool on);

/**
 * Toggle LED. Switches to manual mode.
 */
void led_control_toggle(void);

/**
 * Set LED to auto mode (heartbeat controlled by main loop).
 */
void led_control_auto(void);

/**
 * Update auto-mode LED based on WiFi state.
 * Call periodically from main loop. No-op if in manual mode.
 * @param wifi_connected  true if WiFi is connected
 */
void led_control_heartbeat(bool wifi_connected);

/**
 * Get current LED state.
 * @return true if LED is on, false if off
 */
bool led_control_get_state(void);

/**
 * Check if LED is in manual mode.
 * @return true if manual, false if auto
 */
bool led_control_is_manual(void);

#endif /* LED_CONTROL_H */
