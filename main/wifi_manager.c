#include "wifi_manager.h"

#include <stdlib.h>
#include <string.h>

#include "config_store.h"
#include "esp_eap_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/inet.h"

static const char *TAG = "wifi_manager";

// SoftAP network: 192.168.4.1/24 (matches the legacy Arduino defaults).
#define AP_IP_ADDR "192.168.4.1"
#define AP_GW_ADDR "192.168.4.1"
#define AP_NETMASK "255.255.255.0"
#define AP_MAX_CONN 4

// Station connect attempt budget (~10 s, mirrors the old 100 x 100 ms loop).
#define STA_CONNECT_TIMEOUT_MS 10000
#define STA_CONNECT_ATTEMPTS 3

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

#define MAX_SCAN_RECORDS 32

static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static EventGroupHandle_t s_wifi_events;
static bool s_started;

// ---------------------------------------------------------------------------
// Event handling
// ---------------------------------------------------------------------------

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const char *enc_to_string(wifi_auth_mode_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN:            return "OPEN";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA-PSK";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2-PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2-PSK";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENTERPRISE";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3-PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3-PSK";
    default:                        return "UNKNOWN";
    }
}

static void format_mac(const uint8_t mac[6], char *out, size_t out_len)
{
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void format_ip(esp_netif_t *netif, char *out, size_t out_len)
{
    esp_netif_ip_info_t ip;
    if (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
        snprintf(out, out_len, IPSTR, IP2STR(&ip.ip));
    } else if (out_len) {
        out[0] = '\0';
    }
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

esp_err_t wifi_manager_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    s_wifi_events = xEventGroupCreate();

    // Credentials are persisted in our own NVS namespace, not the driver's.
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    return ESP_OK;
}

static void ensure_started(void)
{
    if (!s_started) {
        ESP_ERROR_CHECK(esp_wifi_start());
        s_started = true;
    }
}

// ---------------------------------------------------------------------------
// SoftAP
// ---------------------------------------------------------------------------

static void setup_softap(void)
{
    ESP_LOGI(TAG, "Setting up SoftAP...");

    char ssid[CONFIG_SSID_MAX_LEN];
    char password[CONFIG_PASS_MAX_LEN];
    config_store_get_ap_ssid(ssid, sizeof(ssid));
    config_store_get_ap_password(password, sizeof(password));
    int channel = config_store_get_ap_channel();
    bool hidden = config_store_get_ap_hidden();

    // Keep the station enabled so scanning/connecting still works in AP mode.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(ssid);
    strlcpy((char *)ap.ap.password, password, sizeof(ap.ap.password));
    ap.ap.channel = (uint8_t)channel;
    ap.ap.ssid_hidden = hidden ? 1 : 0;
    ap.ap.max_connection = AP_MAX_CONN;
    ap.ap.authmode = (strlen(password) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));

    ensure_started();

    // Pin the AP subnet to 192.168.4.1/24.
    esp_netif_ip_info_t ip = {0};
    ip.ip.addr = ipaddr_addr(AP_IP_ADDR);
    ip.gw.addr = ipaddr_addr(AP_GW_ADDR);
    ip.netmask.addr = ipaddr_addr(AP_NETMASK);
    esp_netif_dhcps_stop(s_ap_netif);
    esp_netif_set_ip_info(s_ap_netif, &ip);
    esp_netif_dhcps_start(s_ap_netif);

    char ap_ip[16];
    format_ip(s_ap_netif, ap_ip, sizeof(ap_ip));
    ESP_LOGI(TAG, "AP SSID: %s", ssid);
    ESP_LOGI(TAG, "AP IP address: %s", ap_ip);
}

// ---------------------------------------------------------------------------
// Station connect
// ---------------------------------------------------------------------------

bool wifi_manager_connect(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0) {
        return false;
    }
    password = password ? password : "";

    ensure_started();
    esp_wifi_disconnect();  // drop any prior association

    wifi_config_t sta = {0};
    strlcpy((char *)sta.sta.ssid, ssid, sizeof(sta.sta.ssid));

    // "identity|password" => WPA2-Enterprise (PEAP/MSCHAPv2). Mirrors the
    // legacy convention that keeps both kinds of creds in one password field.
    const char *sep = strchr(password, '|');
    if (sep) {
        ESP_LOGI(TAG, "Connecting using WPA2 Enterprise credentials");
        size_t user_len = (size_t)(sep - password);
        const char *userpass = sep + 1;
        size_t pass_len = strlen(userpass);

        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
        esp_eap_client_set_identity((const uint8_t *)password, user_len);
        esp_eap_client_set_username((const uint8_t *)password, user_len);
        esp_eap_client_set_password((const uint8_t *)userpass, pass_len);
        esp_wifi_sta_enterprise_enable();
    } else {
        esp_wifi_sta_enterprise_disable();  // in case a prior attempt enabled it
        strlcpy((char *)sta.sta.password, password, sizeof(sta.sta.password));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    }

    ESP_LOGI(TAG, "Connecting to %s ...", ssid);
    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdTRUE, pdFALSE, pdMS_TO_TICKS(STA_CONNECT_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        config_store_save_wifi_credentials(ssid, password);
        char ip[16];
        format_ip(s_sta_netif, ip, sizeof(ip));
        ESP_LOGI(TAG, "Connected to %s, STA IP: %s", ssid, ip);
        return true;
    }

    esp_wifi_disconnect();
    ESP_LOGW(TAG, "Failed to connect to %s", ssid);
    return false;
}

// ---------------------------------------------------------------------------
// Auto-connect
// ---------------------------------------------------------------------------

bool wifi_manager_auto_connect(void)
{
    char hostname[CONFIG_HOST_MAX_LEN];
    config_store_get_hostname(hostname, sizeof(hostname));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    esp_netif_set_hostname(s_sta_netif, hostname);
    ensure_started();

    char ssid[CONFIG_SSID_MAX_LEN];
    char password[CONFIG_PASS_MAX_LEN];
    config_store_get_wifi_ssid(ssid, sizeof(ssid));
    config_store_get_wifi_password(password, sizeof(password));

    bool connected = false;
    if (strlen(ssid) > 0) {
        for (int attempt = 1; attempt <= STA_CONNECT_ATTEMPTS && !connected; attempt++) {
            ESP_LOGI(TAG, "[%d] Attempting saved WiFi credentials", attempt);
            connected = wifi_manager_connect(ssid, password);
        }
    }

    // Bring up the AP if it is enabled, or as a fallback when the STA failed.
    if (config_store_get_ap_status() || !connected) {
        setup_softap();
    }

    return connected;
}

void wifi_manager_disconnect(void)
{
    esp_wifi_disconnect();
    config_store_save_wifi_credentials("", "");
}

// ---------------------------------------------------------------------------
// Scan
// ---------------------------------------------------------------------------

static int cmp_rssi_desc(const void *a, const void *b)
{
    const wifi_ap_record_t *ra = a;
    const wifi_ap_record_t *rb = b;
    return rb->rssi - ra->rssi;
}

int wifi_manager_scan(wifi_scan_entry_t *entries, int max_entries)
{
    if (max_entries <= 0) {
        return 0;
    }

    wifi_scan_config_t scan_cfg = {.show_hidden = true};
    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK) {
        return 0;
    }

    uint16_t found = 0;
    esp_wifi_scan_get_ap_num(&found);
    if (found == 0) {
        return 0;
    }
    if (found > MAX_SCAN_RECORDS) {
        found = MAX_SCAN_RECORDS;
    }

    wifi_ap_record_t *records = calloc(found, sizeof(wifi_ap_record_t));
    if (!records) {
        esp_wifi_clear_ap_list();  // release the driver-held scan results
        return 0;
    }
    esp_wifi_scan_get_ap_records(&found, records);

    // Currently associated AP, for the "connected" flag.
    wifi_ap_record_t current;
    bool sta_connected = (esp_wifi_sta_get_ap_info(&current) == ESP_OK);

    qsort(records, found, sizeof(wifi_ap_record_t), cmp_rssi_desc);

    int count = (found < (uint16_t)max_entries) ? found : max_entries;
    for (int i = 0; i < count; i++) {
        wifi_scan_entry_t *e = &entries[i];
        const wifi_ap_record_t *r = &records[i];

        bool connected = sta_connected &&
                         memcmp(current.bssid, r->bssid, 6) == 0;
        bool hidden = (r->ssid[0] == '\0');

        if (hidden) {
            strlcpy(e->ssid,
                    connected ? (const char *)current.ssid : "Hidden Network",
                    sizeof(e->ssid));
        } else {
            strlcpy(e->ssid, (const char *)r->ssid, sizeof(e->ssid));
        }
        e->hidden = hidden;
        e->rssi = r->rssi;
        strlcpy(e->enc, enc_to_string(r->authmode), sizeof(e->enc));
        format_mac(r->bssid, e->mac, sizeof(e->mac));
        e->channel = r->primary;
        e->connected = connected;
    }

    free(records);
    return count;
}

// ---------------------------------------------------------------------------
// Status helpers
// ---------------------------------------------------------------------------

bool wifi_manager_sta_connected(void)
{
    wifi_ap_record_t rec;
    return esp_wifi_sta_get_ap_info(&rec) == ESP_OK;
}

bool wifi_manager_ap_enabled(void)
{
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) != ESP_OK) {
        return false;
    }
    return mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA;
}

void wifi_manager_get_sta_ssid(char *out, size_t out_len)
{
    wifi_ap_record_t rec;
    if (esp_wifi_sta_get_ap_info(&rec) == ESP_OK) {
        strlcpy(out, (const char *)rec.ssid, out_len);
    } else if (out_len) {
        out[0] = '\0';
    }
}

void wifi_manager_get_sta_ip(char *out, size_t out_len)
{
    if (wifi_manager_sta_connected()) {
        format_ip(s_sta_netif, out, out_len);
    } else if (out_len) {
        out[0] = '\0';
    }
}

void wifi_manager_get_ap_ssid(char *out, size_t out_len)
{
    wifi_config_t cfg;
    if (esp_wifi_get_config(WIFI_IF_AP, &cfg) == ESP_OK) {
        strlcpy(out, (const char *)cfg.ap.ssid, out_len);
    } else if (out_len) {
        out[0] = '\0';
    }
}

void wifi_manager_get_ap_ip(char *out, size_t out_len)
{
    format_ip(s_ap_netif, out, out_len);
}

void wifi_manager_get_mac(char *out, size_t out_len)
{
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    format_mac(mac, out, out_len);
}
