#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A single scan result, pre-formatted for the /api/scan JSON response. */
typedef struct {
    char ssid[33];     // "Hidden Network" placeholder for hidden APs
    bool hidden;       // true when the broadcast SSID was empty
    int8_t rssi;
    char enc[20];      // human-readable encryption type
    char mac[18];      // "AA:BB:CC:DD:EE:FF"
    uint8_t channel;
    bool connected;    // true when this is the AP we are currently joined to
} wifi_scan_entry_t;

/**
 * @brief Initialize netif, the default event loop and the WiFi driver.
 *        Creates both the STA and AP default netifs. Does not connect.
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Try the saved station credentials (up to 3 attempts); fall back to
 *        the SoftAP if AP mode is enabled or the station fails to connect.
 * @return true if the station connected.
 */
bool wifi_manager_auto_connect(void);

/**
 * @brief Connect the station to a network, persisting credentials on success.
 *        A password containing '|' is treated as WPA2-Enterprise
 *        ("identity|password"); otherwise WPA2-PSK (or open if empty).
 * @return true on success.
 */
bool wifi_manager_connect(const char *ssid, const char *password);

/** @brief Disconnect the station and clear the saved credentials. */
void wifi_manager_disconnect(void);

/**
 * @brief Scan for nearby networks (including hidden), sorted by RSSI.
 * @param entries    Caller-provided output array.
 * @param max_entries Capacity of @p entries.
 * @return number of entries written.
 */
int wifi_manager_scan(wifi_scan_entry_t *entries, int max_entries);

// --- Status helpers (for /api/status and /api board info) ---
bool wifi_manager_sta_connected(void);
bool wifi_manager_ap_enabled(void);
void wifi_manager_get_sta_ssid(char *out, size_t out_len);
void wifi_manager_get_sta_ip(char *out, size_t out_len);   // "" if not connected
void wifi_manager_get_ap_ssid(char *out, size_t out_len);
void wifi_manager_get_ap_ip(char *out, size_t out_len);
void wifi_manager_get_mac(char *out, size_t out_len);       // station MAC string

#ifdef __cplusplus
}
#endif
