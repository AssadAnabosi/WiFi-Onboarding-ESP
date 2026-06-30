#include "config_store.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "config_store";

// Single NVS namespace holding every setting.
#define NS "config"

// NVS keys (must each be <= 15 characters).
#define KEY_WIFI_SSID "wifi_ssid"
#define KEY_WIFI_PASS "wifi_pass"
#define KEY_AP_SSID "ap_ssid"
#define KEY_AP_PASS "ap_pass"
#define KEY_AP_CHANNEL "ap_channel"
#define KEY_AP_HIDDEN "ap_hidden"
#define KEY_AP_STATUS "ap_status"
#define KEY_HOSTNAME "hostname"

// Factory defaults (mirror the legacy config.json defaults).
#define DEF_AP_SSID "ESP-Access-Point"
#define DEF_AP_PASS "12345678"
#define DEF_AP_CHANNEL 6
#define DEF_AP_HIDDEN false
#define DEF_AP_STATUS true
#define DEF_HOSTNAME "ESP-IoT"

// ---------------------------------------------------------------------------
// Internal NVS helpers
// ---------------------------------------------------------------------------

static void get_str(const char *key, char *out, size_t out_len, const char *def)
{
    nvs_handle_t handle;
    if (nvs_open(NS, NVS_READONLY, &handle) == ESP_OK) {
        size_t len = out_len;
        esp_err_t err = nvs_get_str(handle, key, out, &len);
        nvs_close(handle);
        if (err == ESP_OK) {
            return;
        }
    }
    strlcpy(out, def, out_len);
}

static int32_t get_i32(const char *key, int32_t def)
{
    nvs_handle_t handle;
    int32_t value = def;
    if (nvs_open(NS, NVS_READONLY, &handle) == ESP_OK) {
        if (nvs_get_i32(handle, key, &value) != ESP_OK) {
            value = def;
        }
        nvs_close(handle);
    }
    return value;
}

static bool get_bool(const char *key, bool def)
{
    nvs_handle_t handle;
    uint8_t value = def ? 1 : 0;
    if (nvs_open(NS, NVS_READONLY, &handle) == ESP_OK) {
        if (nvs_get_u8(handle, key, &value) != ESP_OK) {
            value = def ? 1 : 0;
        }
        nvs_close(handle);
    }
    return value != 0;
}

static void set_str(nvs_handle_t handle, const char *key, const char *value)
{
    esp_err_t err = nvs_set_str(handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write %s: %s", key, esp_err_to_name(err));
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t config_store_init(void)
{
    // Ensure the namespace exists so the first write never surprises us.
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }
    nvs_close(handle);

    char ssid[CONFIG_SSID_MAX_LEN];
    char pass[CONFIG_PASS_MAX_LEN];
    char host[CONFIG_HOST_MAX_LEN];

    ESP_LOGI(TAG, "----- Current Settings -----");
    config_store_get_wifi_ssid(ssid, sizeof(ssid));
    ESP_LOGI(TAG, "WiFi SSID: %s", ssid);
    config_store_get_hostname(host, sizeof(host));
    ESP_LOGI(TAG, "Hostname: %s", host);
    config_store_get_ap_ssid(ssid, sizeof(ssid));
    ESP_LOGI(TAG, "AP SSID: %s", ssid);
    config_store_get_ap_password(pass, sizeof(pass));
    ESP_LOGI(TAG, "AP Password: %s", pass);
    ESP_LOGI(TAG, "AP Channel: %d", config_store_get_ap_channel());
    ESP_LOGI(TAG, "AP Hidden: %s", config_store_get_ap_hidden() ? "Yes" : "No");
    ESP_LOGI(TAG, "AP Status: %s", config_store_get_ap_status() ? "Enabled" : "Disabled");
    ESP_LOGI(TAG, "-----------------------------");

    return ESP_OK;
}

void config_store_get_wifi_ssid(char *out, size_t out_len)
{
    get_str(KEY_WIFI_SSID, out, out_len, "");
}

void config_store_get_wifi_password(char *out, size_t out_len)
{
    get_str(KEY_WIFI_PASS, out, out_len, "");
}

void config_store_save_wifi_credentials(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    if (nvs_open(NS, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    set_str(handle, KEY_WIFI_SSID, ssid ? ssid : "");
    set_str(handle, KEY_WIFI_PASS, password ? password : "");
    nvs_commit(handle);
    nvs_close(handle);
}

void config_store_get_ap_ssid(char *out, size_t out_len)
{
    get_str(KEY_AP_SSID, out, out_len, DEF_AP_SSID);
}

void config_store_get_ap_password(char *out, size_t out_len)
{
    get_str(KEY_AP_PASS, out, out_len, DEF_AP_PASS);
}

int config_store_get_ap_channel(void)
{
    return (int)get_i32(KEY_AP_CHANNEL, DEF_AP_CHANNEL);
}

bool config_store_get_ap_hidden(void)
{
    return get_bool(KEY_AP_HIDDEN, DEF_AP_HIDDEN);
}

bool config_store_get_ap_status(void)
{
    return get_bool(KEY_AP_STATUS, DEF_AP_STATUS);
}

void config_store_get_hostname(char *out, size_t out_len)
{
    get_str(KEY_HOSTNAME, out, out_len, DEF_HOSTNAME);
}

void config_store_save_settings(const char *ap_ssid, const char *ap_password,
                                int ap_channel, bool ap_hidden, bool ap_status,
                                const char *hostname)
{
    nvs_handle_t handle;
    if (nvs_open(NS, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    set_str(handle, KEY_AP_SSID, ap_ssid ? ap_ssid : DEF_AP_SSID);
    set_str(handle, KEY_AP_PASS, ap_password ? ap_password : DEF_AP_PASS);
    nvs_set_i32(handle, KEY_AP_CHANNEL, ap_channel);
    nvs_set_u8(handle, KEY_AP_HIDDEN, ap_hidden ? 1 : 0);
    nvs_set_u8(handle, KEY_AP_STATUS, ap_status ? 1 : 0);
    set_str(handle, KEY_HOSTNAME, hostname ? hostname : DEF_HOSTNAME);
    nvs_commit(handle);
    nvs_close(handle);
}

void config_store_factory_reset(void)
{
    nvs_handle_t handle;
    if (nvs_open(NS, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }
    // Getters fall back to defaults for the now-empty namespace.
    config_store_init();
}
