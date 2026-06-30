#include "config_store.h"
#include "esp_log.h"
#include "http_server.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "wifi_manager.h"

static const char *TAG = "app";

// Hostname under which the device is reachable as "config.local".
#define MDNS_HOSTNAME "config"

static void start_mdns(void)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error starting mDNS responder: %s", esp_err_to_name(err));
        return;
    }
    mdns_hostname_set(MDNS_HOSTNAME);
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS responder started: %s.local", MDNS_HOSTNAME);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Running on platform: ESP32");

    // Initialize NVS, recreating it if the layout changed or it is full.
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(config_store_init());
    ESP_ERROR_CHECK(wifi_manager_init());
    wifi_manager_auto_connect();
    ESP_ERROR_CHECK(http_server_start());
    start_mdns();
}
