/*
 * Copyright (c) Arduino s.r.l. and/or its affiliated companies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "WiFi.h"
#include <zephyr/net/ethernet.h>

WiFiClass WiFi;

String WiFiClass::firmwareVersion() {
#if defined(ARDUINO_PORTENTA_C33)
	return "v1.5.0";
#else
	return "v0.0.0";
#endif
}

static const struct device *wifi = DEVICE_DT_GET(DT_NODELABEL(wifi));

WiFiClass::WiFiClass() {
	if (!device_is_ready(wifi)) {
		int err = device_init(wifi);
		if (err < 0) {
			hw_initialization_failed = true;
		}
	}
}

bool WiFiClass::net_init() {

	if (hw_initialization_failed) {
		return false;
	}

	// bring up the network interface
	struct net_if *iface = net_if_lookup_by_dev(wifi);
	if (iface == NULL) {
		return false;
	}

	// setting up mac address
	if (iface->if_dev->link_addr.addr[0] == 0x00 && iface->if_dev->link_addr.addr[1] == 0x00) {
		const struct device *dev = iface->if_dev->dev;
		if (dev != NULL && dev->api != NULL) {
			struct net_if_api *api = (struct net_if_api *)dev->api;

			if (api->init != NULL) {
				api->init(iface);
			}
		}
	}

	if (net_if_up(iface) != 0) {
		return false;
	}
	return true;
}

int WiFiClass::begin(const char *ssid, const char *passphrase, wl_enc_type security,
					 bool blocking) {
	ARG_UNUSED(security); // currently unsupported
	if (net_init() == false) {
		return false;
	}

	sta_iface = net_if_get_wifi_sta();

	netif = sta_iface;
	sta_config.ssid = (const uint8_t *)ssid;
	sta_config.ssid_length = strlen(ssid);
	sta_config.psk = (const uint8_t *)passphrase;
	sta_config.psk_length = strlen(passphrase);
	// TODO: change these fields with scan() results
	sta_config.security = WIFI_SECURITY_TYPE_PSK;
	sta_config.channel = WIFI_CHANNEL_ANY;
	sta_config.band = WIFI_FREQ_BAND_2_4_GHZ;
	sta_config.bandwidth = WIFI_FREQ_BANDWIDTH_20MHZ;

	int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, sta_iface, &sta_config,
					   sizeof(struct wifi_connect_req_params));
	if (ret) {
		return false;
	}

	ret = status();
	if (ret != WL_CONNECTED && blocking) {
		// Note: Waiting forever can be risky if the password is wrong!
		net_mgmt_event_wait_on_iface(sta_iface, NET_EVENT_WIFI_CONNECT_RESULT, NULL, NULL, NULL,
									 K_FOREVER);
		if (status() != WL_CONNECTED) {
			return false; // Exit early so we don't hang in
		}
	}

	NetworkInterface::begin(blocking, NET_EVENT_WIFI_MASK);

	return status();
}

bool WiFiClass::beginAP(char *ssid, char *passphrase, int channel, bool blocking) {

	if (net_init() == false) {
		return false;
	}

	if (ap_iface != NULL) {
		return false;
	}
	ap_iface = net_if_get_wifi_sap();
	netif = ap_iface;
	ap_config.ssid = (const uint8_t *)ssid;
	ap_config.ssid_length = strlen(ssid);
	ap_config.psk = (const uint8_t *)passphrase;
	ap_config.psk_length = strlen(passphrase);
	ap_config.security = WIFI_SECURITY_TYPE_PSK;
	ap_config.channel = channel;
	ap_config.band = WIFI_FREQ_BAND_2_4_GHZ;
	ap_config.bandwidth = WIFI_FREQ_BANDWIDTH_20MHZ;
	int ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, ap_iface, &ap_config,
					   sizeof(struct wifi_connect_req_params));
	if (ret) {
		return false;
	}
	enable_dhcpv4_server(ap_iface);
	if (blocking) {
		net_mgmt_event_wait_on_iface(ap_iface, NET_EVENT_WIFI_AP_ENABLE_RESULT, NULL, NULL, NULL,
									 K_FOREVER);
	}
	return true;
}

int WiFiClass::status() {
	sta_iface = net_if_get_wifi_sta();
	netif = sta_iface;
	if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, netif, &sta_state,
				 sizeof(struct wifi_iface_status))) {
		return WL_NO_SHIELD;
	}
	if (sta_state.state >= WIFI_STATE_ASSOCIATED) {
		return WL_CONNECTED;
	} else {
		return WL_DISCONNECTED;
	}
	return WL_NO_SHIELD;
}

int8_t WiFiClass::scanNetworks() {
	// TODO: borrow code from mbed core for scan results handling
	return 0;
}

char *WiFiClass::SSID() {
	if (status() == WL_CONNECTED) {
		return (char *)sta_state.ssid;
	}
	return nullptr;
}

int32_t WiFiClass::RSSI() {
	if (status() == WL_CONNECTED) {
		return sta_state.rssi;
	}
	return 0;
}
