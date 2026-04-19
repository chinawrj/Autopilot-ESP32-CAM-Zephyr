/*
 * WiFi Manager — Header
 *
 * Manages WiFi STA connection lifecycle: connect, monitor, auto-reconnect.
 * Credentials come from Kconfig (injected at build time via overlay conf).
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <zephyr/net/net_if.h>

/**
 * Initialize WiFi manager and start connection.
 * Registers net_mgmt callbacks and initiates WiFi STA connect.
 *
 * @return 0 on success, negative errno on failure.
 */
int wifi_manager_init(void);

/**
 * Check if WiFi is connected and IP address is assigned.
 */
bool wifi_manager_is_connected(void);

/**
 * Get the assigned IPv4 address as a string.
 * Returns pointer to static buffer, valid until next call.
 *
 * @return IP address string or "0.0.0.0" if not connected.
 */
const char *wifi_manager_get_ip(void);

/**
 * Get WiFi link status (RSSI and channel).
 * Only valid when connected; returns -ENOTCONN otherwise.
 *
 * @param rssi Output: signal strength in dBm (negative value)
 * @param channel Output: WiFi channel number
 * @return 0 on success, negative errno on failure
 */
int wifi_manager_get_link_info(int *rssi, unsigned int *channel);

#endif /* WIFI_MANAGER_H */
