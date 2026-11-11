// /api/health, /api/status, /api/settings, /api/reboot, /api/reset

#include "WebServerManager.h"

#include <AsyncJson.h> // Must come after ESPAsyncWebServer.h
#include <WiFi.h>
#include <esp_psram.h>

#include "../../ProjectConfig.h"
#include "../ota/FirmwareSlots.h"
#include "../ota/UpdateManager.h"
#include "../storage/ConfigStorage.h"
#include "../system/BuildInfo.h"
#include "../system/Restart.h"
#include "../wifi/WiFiManager.h"

namespace
{
constexpr uint32_t RESTART_DELAY_MS = 500;

void fillBoardInfo(JsonObject out)
{
  out["platform"] = "ESP32";
  out["mac"] = WiFi.macAddress();

  JsonObject chip = out["chip_info"].to<JsonObject>();
  chip["chip_model"] = ESP.getChipModel();
  chip["chip_revision"] = ESP.getChipRevision();
  chip["cores"] = ESP.getChipCores();
  chip["cpu_freq_mhz"] = ESP.getCpuFreqMHz();

  JsonObject memory = out["memory"].to<JsonObject>();
  memory["flash_size_mb"] = ESP.getFlashChipSize() / (1024 * 1024);
  memory["free_heap_kb"] = ESP.getFreeHeap() / 1024; // Internal RAM only
  // 0 when the board has no PSRAM or the target doesn't turn it on (see targets/<target>.h).
  // The chip's own size: the heap only maps part of it (4 MB at most on the classic ESP32).
  memory["psram_size_mb"] = psramFound() ? esp_psram_get_size() / (1024 * 1024) : 0;
  memory["free_psram_kb"] = ESP.getFreePsram() / 1024;

  JsonObject software = out["software"].to<JsonObject>();
  software["sdk_version"] = ESP.getSdkVersion();
  software["arduino_core"] = ESP.getCoreVersion();
}

const char *attemptStateName(WiFiManager::AttemptState state)
{
  switch (state)
  {
  case WiFiManager::AttemptState::Connecting:
    return "connecting";
  case WiFiManager::AttemptState::Succeeded:
    return "succeeded";
  case WiFiManager::AttemptState::Failed:
    return "failed";
  default:
    return "none";
  }
}
} // namespace

void WebServerManager::registerSystemRoutes()
{
  _server.on("/api/health", HTTP_GET, [this](AsyncWebServerRequest *request)
             { handleHealth(request); });
  _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request)
             { handleStatus(request); });
  _server.on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest *request)
             { handleGetSettings(request); });
  _server.addHandler(new AsyncCallbackJsonWebHandler("/api/settings", [this](AsyncWebServerRequest *request, JsonVariant &json)
                                                     { handlePostSettings(request, json.as<JsonObject>()); }));
  _server.on("/api/reboot", HTTP_POST, [this](AsyncWebServerRequest *request)
             { handleReboot(request); });
  _server.on("/api/reset", HTTP_POST, [this](AsyncWebServerRequest *request)
             { handleReset(request); });
}

void WebServerManager::handleHealth(AsyncWebServerRequest *request)
{
  JsonDocument doc;
  doc["success"] = true;
  doc["message"] = "What are you doing here ?";
  JsonObject data = doc["data"].to<JsonObject>();
  fillBoardInfo(data);
  data["firmware"]["version"] = FIRMWARE_VERSION;
  data["firmware"]["dev"] = (bool)FIRMWARE_DEV_BUILD;
  sendJson(request, doc);
}

void WebServerManager::handleStatus(AsyncWebServerRequest *request)
{
  JsonDocument doc;

  bool apEnabled = WiFi.getMode() & WIFI_AP;
  doc["softAP"]["enabled"] = apEnabled;
  doc["softAP"]["ssid"] = WiFi.softAPSSID();
  doc["softAP"]["ip"] = apEnabled ? WiFi.softAPIP().toString() : "";

  bool staConnected = WiFi.status() == WL_CONNECTED;
  doc["station"]["connected"] = staConnected;
  doc["station"]["ssid"] = WiFi.SSID();
  doc["station"]["ip"] = staConnected ? WiFi.localIP().toString() : "";
  WiFiManager::Attempt attempt = _wifi.attempt();
  doc["station"]["attempt"]["state"] = attemptStateName(attempt.state);
  if (attempt.state != WiFiManager::AttemptState::None)
    doc["station"]["attempt"]["ssid"] = attempt.ssid;

  doc["version"] = FIRMWARE_VERSION;
  doc["build"] = FIRMWARE_BUILD_DATE;
  doc["commit"] = FIRMWARE_COMMIT;
  doc["dev"] = (bool)FIRMWARE_DEV_BUILD;
  doc["target"] = OTA_TARGET_ID;
  String homeTarget = ConfigStorage::getHomeTarget();
  doc["home_target"] = homeTarget;
  doc["target_mismatch"] = homeTarget != OTA_TARGET_ID;
  doc["previous_version"] = _slots.previousVersion();
  doc["rollback_available"] = _slots.rollbackAvailable();
  // What an update's info block must match, so the browser can check a file before sending it
  doc["project"] = PROJECT_SLUG;
  doc["chip_id"] = CONFIG_IDF_FIRMWARE_CHIP_ID;
  doc["layout"] = _updates.layoutHash();

  sendJson(request, doc);
}

void WebServerManager::handleGetSettings(AsyncWebServerRequest *request)
{
  DeviceSettings settings = ConfigStorage::loadSettings();
  JsonDocument doc;
  doc["ap_ssid"] = settings.apSsid;
  doc["ap_password"] = settings.apPassword;
  doc["ap_channel"] = settings.apChannel;
  doc["ap_hidden"] = settings.apHidden;
  doc["ap_status"] = settings.apEnabled;
  doc["hostname"] = settings.hostname;
  sendJson(request, doc);
}

void WebServerManager::handlePostSettings(AsyncWebServerRequest *request, JsonObject json)
{
  if (refuseWhileUnconfirmed(request))
    return;

  static const char *const required[] = {"ap_ssid", "ap_password", "ap_channel", "ap_hidden", "ap_status", "hostname"};
  String missing;
  for (const char *key : required)
    if (json[key].isNull())
      missing += (missing.isEmpty() ? "" : ", ") + String(key);
  if (!missing.isEmpty())
  {
    JsonDocument doc;
    doc["success"] = false;
    doc["message"] = "Missing required fields";
    doc["fields"] = missing;
    sendJson(request, doc, 400);
    return;
  }

  DeviceSettings settings;
  settings.apSsid = json["ap_ssid"].as<String>();
  settings.apPassword = json["ap_password"].as<String>();
  settings.apChannel = json["ap_channel"].as<int>();
  settings.apHidden = json["ap_hidden"].as<bool>();
  settings.apEnabled = json["ap_status"].as<bool>();
  settings.hostname = json["hostname"].as<String>();
  ConfigStorage::saveSettings(settings);
  ConfigStorage::logSettings();

  sendMessage(request, "Settings updated");
  Restart::schedule(RESTART_DELAY_MS);
}

void WebServerManager::handleReboot(AsyncWebServerRequest *request)
{
  if (refuseWhileUnconfirmed(request))
    return;
  sendMessage(request, "Device is rebooting...");
  Restart::schedule(RESTART_DELAY_MS);
}

void WebServerManager::handleReset(AsyncWebServerRequest *request)
{
  if (refuseWhileUnconfirmed(request))
    return;
  ConfigStorage::factoryReset();
  sendMessage(request, "Resetting to factory settings...");
  Restart::schedule(RESTART_DELAY_MS);
}
