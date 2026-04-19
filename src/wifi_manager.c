/*
 * WiFi Manager — Implementation
 *
 * Uses Zephyr wifi_mgmt API for STA connection.
 * Driver-level auto-reconnect via CONFIG_ESP32_WIFI_STA_RECONNECT.
 * Monitors NET_EVENT_IPV4_ADDR_ADD for DHCP IP assignment.
 */

#include "wifi_manager.h"

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/logging/log.h>
#include <esp_wifi.h>

LOG_MODULE_REGISTER(wifi_mgr, LOG_LEVEL_INF);

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;

static K_SEM_DEFINE(wifi_connected_sem, 0, 1);
static bool connected;
static char ip_addr_str[NET_IPV4_ADDR_LEN] = "0.0.0.0";
static uint32_t disconnect_count;
static uint32_t reconnect_count;

#define WIFI_MGMT_EVENTS (NET_EVENT_WIFI_CONNECT_RESULT | \
			  NET_EVENT_WIFI_DISCONNECT_RESULT)

static void wifi_event_handler(struct net_mgmt_event_callback *cb,
			       uint32_t mgmt_event,
			       struct net_if *iface)
{
	if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
		const struct wifi_status *status =
			(const struct wifi_status *)cb->info;

		if (status->status == 0) {
			LOG_INF("WiFi connected");
		} else {
			LOG_ERR("WiFi connection failed: %d", status->status);
		}
	} else if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		const struct wifi_status *status =
			(const struct wifi_status *)cb->info;

		LOG_WRN("WiFi disconnected: reason=%d", status->status);
		disconnect_count++;
		connected = false;
	}
}

static void ipv4_event_handler(struct net_mgmt_event_callback *cb,
			       uint32_t mgmt_event,
			       struct net_if *iface)
{
	if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD) {
		return;
	}

	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		if (iface->config.ip.ipv4->unicast[i].ipv4.addr_type !=
		    NET_ADDR_DHCP) {
			continue;
		}

		net_addr_ntop(AF_INET,
			&iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr,
			ip_addr_str, sizeof(ip_addr_str));

		LOG_INF("DHCP IP assigned: %s", ip_addr_str);
		if (connected) {
			/* IP changed while already connected (rare) */
		} else if (disconnect_count > 0) {
			reconnect_count++;
			LOG_INF("WiFi reconnected (#%u)", reconnect_count);
		}
		connected = true;
		k_sem_give(&wifi_connected_sem);
		return;
	}
}

static int wifi_connect(void)
{
	struct net_if *iface = net_if_get_default();

	if (!iface) {
		LOG_ERR("No default network interface");
		return -ENODEV;
	}

	const char *ssid = CONFIG_APP_WIFI_SSID;
	const char *psk = CONFIG_APP_WIFI_PASSWORD;

	if (strlen(ssid) == 0) {
		LOG_ERR("WiFi SSID not configured (CONFIG_APP_WIFI_SSID)");
		return -EINVAL;
	}

	struct wifi_connect_req_params params = {
		.ssid = (const uint8_t *)ssid,
		.ssid_length = strlen(ssid),
		.psk = (const uint8_t *)psk,
		.psk_length = strlen(psk),
		.channel = WIFI_CHANNEL_ANY,
		.band = WIFI_FREQ_BAND_2_4_GHZ,
		.security = (strlen(psk) > 0) ? WIFI_SECURITY_TYPE_PSK
					      : WIFI_SECURITY_TYPE_NONE,
	};

	LOG_INF("Connecting to WiFi SSID: %s", ssid);

	int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params,
			   sizeof(params));
	if (ret) {
		LOG_ERR("WiFi connect request failed: %d", ret);
	}

	return ret;
}

int wifi_manager_init(void)
{
	/* Register WiFi event callback */
	net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler,
				     WIFI_MGMT_EVENTS);
	net_mgmt_add_event_callback(&wifi_cb);

	/* Register IPv4 event callback for DHCP */
	net_mgmt_init_event_callback(&ipv4_cb, ipv4_event_handler,
				     NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_cb);

	/* Initiate connection */
	int ret = wifi_connect();

	if (ret) {
		return ret;
	}

	/* Wait for IP assignment with timeout */
	ret = k_sem_take(&wifi_connected_sem,
			 K_SECONDS(CONFIG_APP_WIFI_CONNECT_TIMEOUT_S));
	if (ret) {
		LOG_WRN("WiFi connection timeout (%ds), continuing...",
			CONFIG_APP_WIFI_CONNECT_TIMEOUT_S);
		return -ETIMEDOUT;
	}

	/* Disable WiFi power save for reliable streaming */
	esp_err_t ps_ret = esp_wifi_set_ps(WIFI_PS_NONE);
	if (ps_ret != ESP_OK) {
		LOG_WRN("Failed to disable WiFi PS: 0x%x (non-fatal)", ps_ret);
	} else {
		LOG_INF("WiFi power save disabled (ESP-IDF)");
	}

	return 0;
}

bool wifi_manager_is_connected(void)
{
	return connected;
}

const char *wifi_manager_get_ip(void)
{
	return ip_addr_str;
}

int wifi_manager_get_link_info(int *rssi, unsigned int *channel)
{
	if (!connected) {
		return -ENOTCONN;
	}

	struct net_if *iface = net_if_get_default();

	if (!iface) {
		return -ENODEV;
	}

	struct wifi_iface_status status = {0};
	int ret = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface,
			   &status, sizeof(status));

	if (ret) {
		return ret;
	}

	if (status.state < WIFI_STATE_ASSOCIATED) {
		return -ENOTCONN;
	}

	if (rssi) {
		*rssi = status.rssi;
	}
	if (channel) {
		*channel = status.channel;
	}

	return 0;
}

void wifi_manager_get_stats(uint32_t *disconnects, uint32_t *reconnects)
{
	if (disconnects) {
		*disconnects = disconnect_count;
	}
	if (reconnects) {
		*reconnects = reconnect_count;
	}
}
