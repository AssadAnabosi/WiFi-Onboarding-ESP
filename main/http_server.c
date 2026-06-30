#include "http_server.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "config_store.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_manager.h"

static const char *TAG = "http_server";

#define MAX_BODY_LEN 1024
#define MAX_SCAN_ENTRIES 32
#define RESTART_DELAY_MS 200

// ---------------------------------------------------------------------------
// Embedded, gzip-compressed web assets (produced by utils/build_web.py and
// embedded via EMBED_FILES in main/CMakeLists.txt). Symbol names are derived
// from each file's basename with '.' replaced by '_'.
// ---------------------------------------------------------------------------

#define DECLARE_ASSET(name)                                              \
    extern const uint8_t name##_start[] asm("_binary_" #name "_start");  \
    extern const uint8_t name##_end[] asm("_binary_" #name "_end")

DECLARE_ASSET(index_html_gz);
DECLARE_ASSET(home_html_gz);
DECLARE_ASSET(scan_html_gz);
DECLARE_ASSET(settings_html_gz);

DECLARE_ASSET(index_css_gz);
DECLARE_ASSET(home_css_gz);
DECLARE_ASSET(scan_css_gz);
DECLARE_ASSET(settings_css_gz);
DECLARE_ASSET(toast_css_gz);
DECLARE_ASSET(modal_css_gz);

DECLARE_ASSET(index_js_gz);
DECLARE_ASSET(home_js_gz);
DECLARE_ASSET(scan_js_gz);
DECLARE_ASSET(settings_js_gz);
DECLARE_ASSET(modal_js_gz);
DECLARE_ASSET(toast_js_gz);

DECLARE_ASSET(favicon_ico_gz);

typedef struct {
    const char *uri;
    const uint8_t *start;
    const uint8_t *end;
    const char *content_type;
} static_asset_t;

#define ASSET_ENTRY(path, name, ctype) {path, name##_start, name##_end, ctype}

static const static_asset_t k_assets[] = {
    ASSET_ENTRY("/", index_html_gz, "text/html"),
    ASSET_ENTRY("/home", home_html_gz, "text/html"),
    ASSET_ENTRY("/scan", scan_html_gz, "text/html"),
    ASSET_ENTRY("/settings", settings_html_gz, "text/html"),

    ASSET_ENTRY("/css/index.css", index_css_gz, "text/css"),
    ASSET_ENTRY("/css/home.css", home_css_gz, "text/css"),
    ASSET_ENTRY("/css/scan.css", scan_css_gz, "text/css"),
    ASSET_ENTRY("/css/settings.css", settings_css_gz, "text/css"),
    ASSET_ENTRY("/css/toast.css", toast_css_gz, "text/css"),
    ASSET_ENTRY("/css/modal.css", modal_css_gz, "text/css"),

    ASSET_ENTRY("/js/index.js", index_js_gz, "application/javascript"),
    ASSET_ENTRY("/js/home.js", home_js_gz, "application/javascript"),
    ASSET_ENTRY("/js/scan.js", scan_js_gz, "application/javascript"),
    ASSET_ENTRY("/js/settings.js", settings_js_gz, "application/javascript"),
    ASSET_ENTRY("/js/modal.js", modal_js_gz, "application/javascript"),
    ASSET_ENTRY("/js/toast.js", toast_js_gz, "application/javascript"),

    ASSET_ENTRY("/favicon.ico", favicon_ico_gz, "image/x-icon"),
};

// ---------------------------------------------------------------------------
// JSON response helpers (consistent {success, message, data?} shape)
// ---------------------------------------------------------------------------

static const char *status_str(int code)
{
    switch (code) {
    case 200: return "200 OK";
    case 400: return "400 Bad Request";
    case 404: return "404 Not Found";
    default:  return "500 Internal Server Error";
    }
}

// Serializes and sends @p root, taking ownership (always deletes it).
static esp_err_t send_json(httpd_req_t *req, cJSON *root, int code)
{
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_status(req, status_str(code));
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, out);
    free(out);
    return err;
}

// Returns the string value of object[key], or def if absent/not a string.
static const char *json_str(const cJSON *obj, const char *key, const char *def)
{
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    return (cJSON_IsString(item) && item->valuestring) ? item->valuestring : def;
}

static esp_err_t send_message(httpd_req_t *req, const char *message, int code)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", code == 200);
    cJSON_AddStringToObject(root, "message", message);
    ESP_LOGI(TAG, "%s", message);
    return send_json(req, root, code);
}

// Reads the full request body into a NUL-terminated heap buffer (caller frees).
static char *read_body(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > MAX_BODY_LEN) {
        return NULL;
    }
    char *buf = malloc(total + 1);
    if (!buf) {
        return NULL;
    }
    int received = 0;
    while (received < total) {
        int r = httpd_req_recv(req, buf + received, total - received);
        if (r <= 0) {
            free(buf);
            return NULL;
        }
        received += r;
    }
    buf[total] = '\0';
    return buf;
}

// ---------------------------------------------------------------------------
// Static asset serving (wildcard GET handler, registered last)
// ---------------------------------------------------------------------------

static esp_err_t serve_asset(httpd_req_t *req, const static_asset_t *a)
{
    httpd_resp_set_type(req, a->content_type);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    return httpd_resp_send(req, (const char *)a->start,
                           (size_t)(a->end - a->start));
}

static esp_err_t static_get_handler(httpd_req_t *req)
{
    for (size_t i = 0; i < sizeof(k_assets) / sizeof(k_assets[0]); i++) {
        if (strcmp(req->uri, k_assets[i].uri) == 0) {
            return serve_asset(req, &k_assets[i]);
        }
    }
    char message[160];
    snprintf(message, sizeof(message), "%s Not Found", req->uri);
    httpd_resp_set_status(req, status_str(404));
    return send_message(req, message, 404);
}

// ---------------------------------------------------------------------------
// API: board info / health (GET /api)
// ---------------------------------------------------------------------------

static const char *chip_model_str(esp_chip_model_t model)
{
    switch (model) {
    case CHIP_ESP32:   return "ESP32";
    case CHIP_ESP32S2: return "ESP32-S2";
    case CHIP_ESP32S3: return "ESP32-S3";
    case CHIP_ESP32C3: return "ESP32-C3";
    case CHIP_ESP32C2: return "ESP32-C2";
    case CHIP_ESP32C6: return "ESP32-C6";
    case CHIP_ESP32H2: return "ESP32-H2";
    default:           return "ESP32";
    }
}

static esp_err_t handle_health(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "message", "What are you doing here ?");

    cJSON *data = cJSON_AddObjectToObject(root, "data");
    cJSON_AddStringToObject(data, "platform", "ESP32");

    char mac[18];
    wifi_manager_get_mac(mac, sizeof(mac));
    cJSON_AddStringToObject(data, "mac", mac);

    esp_chip_info_t info;
    esp_chip_info(&info);
    cJSON *chip = cJSON_AddObjectToObject(data, "chip_info");
    cJSON_AddStringToObject(chip, "chip_model", chip_model_str(info.model));
    cJSON_AddNumberToObject(chip, "chip_revision", info.revision);
    cJSON_AddNumberToObject(chip, "cores", info.cores);
    cJSON_AddNumberToObject(chip, "cpu_freq_mhz", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    cJSON *memory = cJSON_AddObjectToObject(data, "memory");
    cJSON_AddNumberToObject(memory, "flash_size_mb", flash_size / (1024 * 1024));
    cJSON_AddNumberToObject(memory, "free_heap_kb", esp_get_free_heap_size() / 1024);

    cJSON *software = cJSON_AddObjectToObject(data, "software");
    cJSON_AddStringToObject(software, "sdk_version", esp_get_idf_version());
    cJSON_AddStringToObject(software, "idf_target", CONFIG_IDF_TARGET);

    return send_json(req, root, 200);
}

// ---------------------------------------------------------------------------
// API: status (GET /api/status)
// ---------------------------------------------------------------------------

static esp_err_t handle_status(httpd_req_t *req)
{
    bool ap_enabled = wifi_manager_ap_enabled();
    bool sta_connected = wifi_manager_sta_connected();
    char buf[64];

    cJSON *root = cJSON_CreateObject();

    cJSON *softap = cJSON_AddObjectToObject(root, "softAP");
    cJSON_AddBoolToObject(softap, "enabled", ap_enabled);
    wifi_manager_get_ap_ssid(buf, sizeof(buf));
    cJSON_AddStringToObject(softap, "ssid", buf);
    if (ap_enabled) {
        wifi_manager_get_ap_ip(buf, sizeof(buf));
        cJSON_AddStringToObject(softap, "ip", buf);
    } else {
        cJSON_AddStringToObject(softap, "ip", "");
    }

    cJSON *station = cJSON_AddObjectToObject(root, "station");
    cJSON_AddBoolToObject(station, "connected", sta_connected);
    wifi_manager_get_sta_ssid(buf, sizeof(buf));
    cJSON_AddStringToObject(station, "ssid", buf);
    if (sta_connected) {
        wifi_manager_get_sta_ip(buf, sizeof(buf));
        cJSON_AddStringToObject(station, "ip", buf);
    } else {
        cJSON_AddStringToObject(station, "ip", "");
    }

    return send_json(req, root, 200);
}

// ---------------------------------------------------------------------------
// API: scan (GET /api/scan) -> top-level JSON array
// ---------------------------------------------------------------------------

static esp_err_t handle_scan(httpd_req_t *req)
{
    wifi_scan_entry_t *entries = calloc(MAX_SCAN_ENTRIES, sizeof(wifi_scan_entry_t));
    if (!entries) {
        return send_message(req, "Out of memory", 500);
    }
    int n = wifi_manager_scan(entries, MAX_SCAN_ENTRIES);

    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", entries[i].ssid);
        cJSON_AddBoolToObject(net, "hidden", entries[i].hidden);
        cJSON_AddNumberToObject(net, "rssi", entries[i].rssi);
        cJSON_AddStringToObject(net, "enc", entries[i].enc);
        cJSON_AddStringToObject(net, "mac", entries[i].mac);
        cJSON_AddNumberToObject(net, "channel", entries[i].channel);
        cJSON_AddBoolToObject(net, "connected", entries[i].connected);
        cJSON_AddItemToArray(root, net);
    }
    free(entries);
    return send_json(req, root, 200);
}

// ---------------------------------------------------------------------------
// API: connect / disconnect
// ---------------------------------------------------------------------------

static esp_err_t handle_connect(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) {
        return send_message(req, "Invalid request body", 400);
    }
    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) {
        return send_message(req, "Invalid JSON", 400);
    }

    cJSON *ssid_item = cJSON_GetObjectItem(json, "ssid");
    if (!cJSON_IsString(ssid_item) || ssid_item->valuestring[0] == '\0') {
        cJSON_Delete(json);
        return send_message(req, "Missing required field: ssid", 400);
    }
    cJSON *pass_item = cJSON_GetObjectItem(json, "password");
    const char *ssid = ssid_item->valuestring;
    const char *password = cJSON_IsString(pass_item) ? pass_item->valuestring : "";

    bool ok = wifi_manager_connect(ssid, password);
    char message[96];
    if (ok) {
        snprintf(message, sizeof(message), "Connected to %s WiFi Network", ssid);
    } else {
        snprintf(message, sizeof(message), "Failed to connect to WiFi Network");
    }
    cJSON_Delete(json);
    return send_message(req, message, ok ? 200 : 400);
}

static esp_err_t handle_disconnect(httpd_req_t *req)
{
    wifi_manager_disconnect();
    return send_message(req, "Disconnected from WiFi Network", 200);
}

// ---------------------------------------------------------------------------
// API: settings (GET/POST)
// ---------------------------------------------------------------------------

static esp_err_t handle_get_settings(httpd_req_t *req)
{
    char ssid[CONFIG_SSID_MAX_LEN];
    char password[CONFIG_PASS_MAX_LEN];
    char hostname[CONFIG_HOST_MAX_LEN];

    cJSON *root = cJSON_CreateObject();
    config_store_get_ap_ssid(ssid, sizeof(ssid));
    cJSON_AddStringToObject(root, "ap_ssid", ssid);
    config_store_get_ap_password(password, sizeof(password));
    cJSON_AddStringToObject(root, "ap_password", password);
    cJSON_AddNumberToObject(root, "ap_channel", config_store_get_ap_channel());
    cJSON_AddBoolToObject(root, "ap_hidden", config_store_get_ap_hidden());
    cJSON_AddBoolToObject(root, "ap_status", config_store_get_ap_status());
    config_store_get_hostname(hostname, sizeof(hostname));
    cJSON_AddStringToObject(root, "hostname", hostname);

    return send_json(req, root, 200);
}

static esp_err_t handle_post_settings(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) {
        return send_message(req, "Invalid request body", 400);
    }
    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) {
        return send_message(req, "Invalid JSON", 400);
    }

    static const char *required[] = {"ap_ssid", "ap_password", "ap_channel",
                                     "ap_hidden", "ap_status", "hostname"};
    char missing[128] = {0};
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
        if (!cJSON_HasObjectItem(json, required[i])) {
            if (missing[0]) {
                strlcat(missing, ", ", sizeof(missing));
            }
            strlcat(missing, required[i], sizeof(missing));
        }
    }
    if (missing[0]) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "success", false);
        cJSON_AddStringToObject(err, "message", "Missing required fields");
        cJSON_AddStringToObject(err, "fields", missing);
        cJSON_Delete(json);
        return send_json(req, err, 400);
    }

    const char *ap_ssid = json_str(json, "ap_ssid", "");
    const char *ap_password = json_str(json, "ap_password", "");
    cJSON *ch_item = cJSON_GetObjectItem(json, "ap_channel");
    int ap_channel = cJSON_IsNumber(ch_item) ? ch_item->valueint : 6;
    bool ap_hidden = cJSON_IsTrue(cJSON_GetObjectItem(json, "ap_hidden"));
    bool ap_status = cJSON_IsTrue(cJSON_GetObjectItem(json, "ap_status"));
    const char *hostname = json_str(json, "hostname", "ESP-IoT");

    config_store_save_settings(ap_ssid, ap_password, ap_channel, ap_hidden,
                               ap_status, hostname);

    ESP_LOGI(TAG, "------- New Settings -------");
    ESP_LOGI(TAG, "Hostname: %s", hostname);
    ESP_LOGI(TAG, "AP SSID: %s", ap_ssid);
    ESP_LOGI(TAG, "AP Channel: %d", ap_channel);
    ESP_LOGI(TAG, "AP Hidden: %s", ap_hidden ? "Yes" : "No");
    ESP_LOGI(TAG, "AP Status: %s", ap_status ? "Enabled" : "Disabled");
    ESP_LOGI(TAG, "-----------------------------");

    cJSON_Delete(json);
    send_message(req, "Settings updated", 200);
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY_MS));
    esp_restart();
    return ESP_OK;  // unreachable
}

// ---------------------------------------------------------------------------
// API: reboot / reset
// ---------------------------------------------------------------------------

static esp_err_t handle_reboot(httpd_req_t *req)
{
    send_message(req, "Device is rebooting...", 200);
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY_MS));
    esp_restart();
    return ESP_OK;  // unreachable
}

static esp_err_t handle_reset(httpd_req_t *req)
{
    send_message(req, "Resetting to factory settings...", 200);
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY_MS));
    config_store_factory_reset();
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY_MS));
    esp_restart();
    return ESP_OK;  // unreachable
}

// ---------------------------------------------------------------------------
// Server bring-up
// ---------------------------------------------------------------------------

static void register_handler(httpd_handle_t server, const char *uri,
                             httpd_method_t method,
                             esp_err_t (*handler)(httpd_req_t *))
{
    httpd_uri_t u = {
        .uri = uri,
        .method = method,
        .handler = handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &u);
}

esp_err_t http_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 16;
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    // Specific API routes first so they take precedence over the wildcard.
    register_handler(server, "/api", HTTP_GET, handle_health);
    register_handler(server, "/api/status", HTTP_GET, handle_status);
    register_handler(server, "/api/scan", HTTP_GET, handle_scan);
    register_handler(server, "/api/settings", HTTP_GET, handle_get_settings);

    register_handler(server, "/api/connect", HTTP_POST, handle_connect);
    register_handler(server, "/api/disconnect", HTTP_POST, handle_disconnect);
    register_handler(server, "/api/settings", HTTP_POST, handle_post_settings);
    register_handler(server, "/api/reboot", HTTP_POST, handle_reboot);
    register_handler(server, "/api/reset", HTTP_POST, handle_reset);

    // Catch-all: static assets + 404. Must be registered last.
    register_handler(server, "/*", HTTP_GET, static_get_handler);

    ESP_LOGI(TAG, "Web Server Started");
    return ESP_OK;
}
