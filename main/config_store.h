#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Buffer sizes (including the NUL terminator) for the stored strings.
#define CONFIG_SSID_MAX_LEN 33   // 802.11 SSID is up to 32 bytes
#define CONFIG_PASS_MAX_LEN 129  // PSK up to 63; larger to fit "user|password" (WPA2-E)
#define CONFIG_HOST_MAX_LEN 33

/**
 * @brief Initialize the NVS-backed configuration store.
 *
 * Opens the underlying NVS namespace and logs the current settings. Missing
 * keys transparently fall back to their defaults in the getters, so no
 * explicit seeding is required.
 */
esp_err_t config_store_init(void);

// --- WiFi station credentials ---
void config_store_get_wifi_ssid(char *out, size_t out_len);
void config_store_get_wifi_password(char *out, size_t out_len);
void config_store_save_wifi_credentials(const char *ssid, const char *password);

// --- Access Point + device settings ---
void config_store_get_ap_ssid(char *out, size_t out_len);
void config_store_get_ap_password(char *out, size_t out_len);
int config_store_get_ap_channel(void);
bool config_store_get_ap_hidden(void);
bool config_store_get_ap_status(void);
void config_store_get_hostname(char *out, size_t out_len);

void config_store_save_settings(const char *ap_ssid, const char *ap_password,
                                int ap_channel, bool ap_hidden, bool ap_status,
                                const char *hostname);

/**
 * @brief Erase all stored configuration, reverting to factory defaults.
 */
void config_store_factory_reset(void);

#ifdef __cplusplus
}
#endif
