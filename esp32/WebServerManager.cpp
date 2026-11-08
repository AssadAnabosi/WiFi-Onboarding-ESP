#include <ESPAsyncWebServer.h>
#include "WebServerManager.h"
#include "WiFiManager.h"
#include "ConfigStorage.h"
#include "ProjectConfig.h"
#include "BuildInfo.h"
#include "WebAssets.h"
#include "UpdateManager.h"
#include <ArduinoJson.h>
#include <AsyncTCP.h>

/**
 * @warning //Must always come after ESPAsyncWebServer.h to resolve potential type declaration issues.
 **/
#include "AsyncJson.h"

// Scan results older than this are discarded and a fresh scan is started
#define SCAN_RESULTS_MAX_AGE_MS 15000

WebServerManager::WebServerManager()
    : server(80) {}

void WebServerManager::begin()
{
  // Cross-site request protection on every POST route
  server.addMiddleware([this](AsyncWebServerRequest *request, ArMiddlewareNext next)
                       {
    if (request->method() == HTTP_POST)
    {
      String reason;
      if (!isTrustedPost(request, reason))
      {
        sendMessage(request, reason, 403);
        return;
      }
    }
    next(); });

  // Pages and static files, embedded in the firmware
  for (size_t i = 0; i < WEB_ASSETS_COUNT; i++)
  {
    const WebAsset *asset = &WEB_ASSETS[i];
    server.on(asset->url, HTTP_GET, [this, asset](AsyncWebServerRequest *request)
              { serveAsset(request, asset); });
  }

  // GET API routes
  server.on("/api/health", HTTP_GET, [this](AsyncWebServerRequest *request)
            { handleHealth(request); });
  server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request)
            { handleStatus(request); });
  server.on("/api/scan", HTTP_GET, [this](AsyncWebServerRequest *request)
            { handleScan(request); });
  server.on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest *request)
            { handleGetSettings(request); });

  // POST API routes with AsyncJson
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/connect", [this](AsyncWebServerRequest *request, JsonVariant &json)
                                                    { handleConnect(request, json.as<JsonObject>()); }));

  server.on("/api/disconnect", HTTP_POST, [this](AsyncWebServerRequest *request)
            { handleDisconnect(request); });

  server.addHandler(new AsyncCallbackJsonWebHandler("/api/settings", [this](AsyncWebServerRequest *request, JsonVariant &json)
                                                    { handlePostSettings(request, json.as<JsonObject>()); }));

  server.on("/api/reboot", HTTP_POST, [this](AsyncWebServerRequest *request)
            { handleReboot(request); });

  server.on("/api/reset", HTTP_POST, [this](AsyncWebServerRequest *request)
            { handleReset(request); });

  registerUpdateRoutes();

  // 404 fallback
  server.onNotFound([this](AsyncWebServerRequest *request)
                    {
    String message = request->url() + " Not Found";
    sendMessage(request, message, 404); });

  server.begin();
  Serial.println("Web Server Started");
}

void WebServerManager::loop()
{
  if (_restartPending && (int32_t)(millis() - _restartAt) >= 0)
  {
    Serial.println("Restarting...");
    ESP.restart();
  }
}

bool WebServerManager::refuseWhileUnconfirmed(AsyncWebServerRequest *request)
{
  if (!updateManager.isUnconfirmed())
    return false;
  sendMessage(request, "Finishing update, please wait " + String(updateManager.confirmSecondsLeft()) + " s", 409);
  return true;
}

void WebServerManager::scheduleRestart(uint32_t delayMs)
{
  _restartAt = millis() + delayMs;
  _restartPending = true;
}

bool WebServerManager::isOwnHost(String host)
{
  host.toLowerCase();
  if (host.isEmpty())
    return false;

  if ((WiFi.getMode() & WIFI_AP) && host == WiFi.softAPIP().toString())
    return true;
  if (WiFi.status() == WL_CONNECTED && host == WiFi.localIP().toString())
    return true;
  if (host == MDNS_SETUP_NAME ".local")
    return true;

  String hostname = WiFi.getHostname();
  hostname.toLowerCase();
  return !hostname.isEmpty() && (host == hostname || host == hostname + ".local");
}

bool WebServerManager::isTrustedPost(AsyncWebServerRequest *request, String &reason)
{
  // Browsers must ask permission before sending these types to another site, and the
  // device never grants it (no CORS headers). Bodiless POSTs must still declare JSON.
  const String &type = request->contentType();
  if (!type.equalsIgnoreCase("application/json") && !type.equalsIgnoreCase("application/octet-stream"))
  {
    reason = "Unsupported content type";
    return false;
  }

  String host = request->host();
  int colon = host.indexOf(':');
  if (colon >= 0)
    host = host.substring(0, colon);
  if (!isOwnHost(host))
  {
    reason = "Request host not allowed";
    return false;
  }

  // Tools such as curl send no Origin; browsers always do on POST
  const AsyncWebHeader *origin = request->getHeader("Origin");
  if (origin)
  {
    String value = origin->value();
    value.toLowerCase();
    if (!value.startsWith("http://"))
    {
      reason = "Request origin not allowed";
      return false;
    }
    String originHost = value.substring(7);
    colon = originHost.indexOf(':');
    if (colon >= 0)
    {
      if (originHost.substring(colon + 1) != "80")
      {
        reason = "Request origin not allowed";
        return false;
      }
      originHost = originHost.substring(0, colon);
    }
    if (!isOwnHost(originHost))
    {
      reason = "Request origin not allowed";
      return false;
    }
  }
  return true;
}

void WebServerManager::sendJson(AsyncWebServerRequest *request, const JsonDocument &doc, int code)
{
  String response;
  serializeJson(doc, response);
  request->send(code, "application/json", response);
}

void WebServerManager::sendMessage(AsyncWebServerRequest *request, const String &message, int code)
{
  JsonDocument doc;
  doc["success"] = (code >= 200 && code < 300);
  doc["message"] = message;
  Serial.println(message);
  sendJson(request, doc, code);
}

void WebServerManager::serveAsset(AsyncWebServerRequest *request, const WebAsset *asset)
{
  // The browser revalidates on every visit, so a new firmware never runs with stale files.
  // The server's built-in 304 handling only covers filesystem files, so compare here.
  const AsyncWebHeader *ifNoneMatch = request->getHeader("If-None-Match");
  if (ifNoneMatch && ifNoneMatch->value().indexOf(asset->etag) >= 0)
  {
    AsyncWebServerResponse *response = request->beginResponse(304);
    response->addHeader("ETag", asset->etag);
    response->addHeader("Cache-Control", "no-cache");
    request->send(response);
    return;
  }
  AsyncWebServerResponse *response = request->beginResponse(200, asset->mime, asset->data, asset->length);
  response->addHeader("Content-Encoding", "gzip");
  response->addHeader("ETag", asset->etag);
  response->addHeader("Cache-Control", "no-cache");
  request->send(response);
}

/**
 * @brief Converts the encryption type to a human-readable string.
 * @param encType The encryption type [received from WiFi.scan] as an integer.
 * @return A string representing the encryption type.
 */

String getEncryptionType(int encType)
{
  switch (encType)
  {
  case WIFI_AUTH_OPEN:
    return "OPEN";
  case WIFI_AUTH_WEP:
    return "WEP";
  case WIFI_AUTH_WPA_PSK:
    return "WPA-PSK";
  case WIFI_AUTH_WPA2_PSK:
    return "WPA2-PSK";
  case WIFI_AUTH_WPA_WPA2_PSK:
    return "WPA/WPA2-PSK";
  case WIFI_AUTH_WPA2_ENTERPRISE:
    return "WPA2-ENTERPRISE";
  case WIFI_AUTH_WPA3_PSK:
    return "WPA3-PSK";
  case WIFI_AUTH_WPA2_WPA3_PSK:
    return "WPA2/WPA3-PSK";
  default:
    return "UNKNOWN";
  }
}

/**
 * @brief Populates the JSON object with board data.
 * @param output The JSON object to populate.
 */
void getBoardData(JsonObject &output)
{
  // Platform
  output["platform"] = "ESP32";

  // MAC Address
  output["mac"] = WiFi.macAddress();

  // Chip Info
  JsonObject chipInfo = output["chip_info"].to<JsonObject>();
  chipInfo["chip_model"] = ESP.getChipModel();
  chipInfo["chip_revision"] = ESP.getChipRevision();
  chipInfo["cores"] = ESP.getChipCores();
  chipInfo["cpu_freq_mhz"] = ESP.getCpuFreqMHz();

  // Memory
  JsonObject memory = output["memory"].to<JsonObject>();
  memory["flash_size_mb"] = ESP.getFlashChipSize() / (1024 * 1024);
  memory["free_heap_kb"] = ESP.getFreeHeap() / 1024;

  // SDK/Arduino versions
  JsonObject software = output["software"].to<JsonObject>();
  software["sdk_version"] = ESP.getSdkVersion();
  software["arduino_core"] = ESP.getCoreVersion();
}

void WebServerManager::handleHealth(AsyncWebServerRequest *request)
{
  JsonDocument doc;

  doc["success"] = true;
  doc["message"] = "What are you doing here ?";

  // Add nested "data" object with board info
  JsonObject data = doc["data"].to<JsonObject>();
  getBoardData(data); // Populate dynamically
  data["firmware"]["version"] = FIRMWARE_VERSION;
  data["firmware"]["dev"] = (bool)FIRMWARE_DEV_BUILD;

  sendJson(request, doc);
}

/**
 * @brief Converts a background connection attempt state to its API name.
 */
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

void WebServerManager::handleStatus(AsyncWebServerRequest *request)
{
  JsonDocument doc;

  bool apEnabled = WiFi.getMode() & WIFI_AP;
  bool staConnected = (WiFi.status() == WL_CONNECTED);

  doc["softAP"]["enabled"] = apEnabled;
  doc["softAP"]["ssid"] = WiFi.softAPSSID();
  doc["softAP"]["ip"] = apEnabled ? WiFi.softAPIP().toString() : "";

  doc["station"]["connected"] = staConnected;
  doc["station"]["ssid"] = WiFi.SSID();
  doc["station"]["ip"] = staConnected ? WiFi.localIP().toString() : "";

  doc["version"] = FIRMWARE_VERSION;
  doc["build"] = FIRMWARE_BUILD_DATE;
  doc["commit"] = FIRMWARE_COMMIT;
  doc["dev"] = (bool)FIRMWARE_DEV_BUILD;
  doc["target"] = OTA_TARGET_ID;
  String homeTarget = ConfigStorage::getHomeTarget();
  doc["home_target"] = homeTarget;
  doc["target_mismatch"] = homeTarget != OTA_TARGET_ID;
  doc["previous_version"] = updateManager.previousVersion();
  doc["rollback_available"] = updateManager.rollbackAvailable();
  // What an update's info block must match, so the browser can check a file before sending it
  doc["project"] = PROJECT_SLUG;
  doc["chip_id"] = CONFIG_IDF_FIRMWARE_CHIP_ID;
  doc["layout"] = updateManager.layoutHash();

  WiFiManager::AttemptState attempt = wifiManager.attemptState();
  doc["station"]["attempt"]["state"] = attemptStateName(attempt);
  if (attempt != WiFiManager::AttemptState::None)
    doc["station"]["attempt"]["ssid"] = wifiManager.attemptSSID();

  sendJson(request, doc);
}

void WebServerManager::handleScan(AsyncWebServerRequest *request)
{
  if (updateManager.isBusy())
  {
    sendMessage(request, "An update is running, try again later", 409);
    return;
  }

  int16_t n = WiFi.scanComplete();

  if (n == WIFI_SCAN_RUNNING)
  {
    sendMessage(request, "Scanning", 202);
    return;
  }

  // Results nobody collected in time are stale: drop them and scan again
  if (n >= 0 && millis() - _scanStartedAt > SCAN_RESULTS_MAX_AGE_MS)
  {
    WiFi.scanDelete();
    n = WIFI_SCAN_FAILED;
  }

  if (n < 0)
  {
    if (wifiManager.isConnecting())
    {
      sendMessage(request, "Connecting to a network, try again shortly", 409);
      return;
    }
    if (WiFi.scanNetworks(true, true) == WIFI_SCAN_FAILED)
    {
      sendMessage(request, "Failed to start WiFi scan", 500);
      return;
    }
    _scanStartedAt = millis();
    sendMessage(request, "Scanning", 202);
    return;
  }

  bool WL_Connected = (WiFi.status() == WL_CONNECTED);
  String currentSSID = WiFi.SSID();
  String currentBSSID = WiFi.BSSIDstr();

  int indices[n];
  for (int i = 0; i < n; ++i)
    indices[i] = i;

  for (int i = 0; i < n - 1; ++i)
  {
    for (int j = i + 1; j < n; ++j)
    {
      if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i]))
      {
        int temp = indices[i];
        indices[i] = indices[j];
        indices[j] = temp;
      }
    }
  }

  JsonDocument doc;
  JsonArray networks = doc.to<JsonArray>();

  for (int k = 0; k < n; ++k)
  {
    int i = indices[k];
    JsonObject network = networks.add<JsonObject>();
    bool isHidden = WiFi.SSID(i).isEmpty();
    bool connected = WL_Connected && (currentBSSID.equals(WiFi.BSSIDstr(i)));
    if (isHidden)
    {
      network["ssid"] = connected ? currentSSID : "Hidden Network";
      network["hidden"] = true;
    }
    else
    {
      network["ssid"] = WiFi.SSID(i);
      network["hidden"] = false;
    }
    network["rssi"] = WiFi.RSSI(i);
    network["enc"] = getEncryptionType(WiFi.encryptionType(i));
    network["mac"] = WiFi.BSSIDstr(i);
    network["channel"] = WiFi.channel(i);
    network["connected"] = connected;
  }

  WiFi.scanDelete();
  sendJson(request, doc);
}

void WebServerManager::handleConnect(AsyncWebServerRequest *request, const JsonObject &json)
{
  if (json["ssid"].isNull())
  {
    sendMessage(request, "Missing required field: ssid", 400);
    return;
  }

  if (updateManager.isBusy())
  {
    sendMessage(request, "An update is running, try again later", 409);
    return;
  }

  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING)
  {
    sendMessage(request, "WiFi scan in progress, try again shortly", 409);
    return;
  }

  String ssid = json["ssid"].as<String>();
  String password = json["password"] | "";

  if (!wifiManager.startConnect(ssid, password))
  {
    if (wifiManager.isConnecting())
      sendMessage(request, "Already connecting to a network", 409);
    else
      sendMessage(request, "Failed to start connecting", 500);
    return;
  }
  sendMessage(request, "Connecting to " + ssid, 202);
}

void WebServerManager::handleDisconnect(AsyncWebServerRequest *request)
{
  WiFi.disconnect(true);
  ConfigStorage::saveWiFiCredentials("", "");
  sendMessage(request, "Disconnected from WiFi Network");
}

void WebServerManager::handleGetSettings(AsyncWebServerRequest *request)
{
  JsonDocument doc;

  doc["ap_ssid"] = ConfigStorage::getAPSSID();
  doc["ap_password"] = ConfigStorage::getAPPassword();
  doc["ap_channel"] = ConfigStorage::getAPChannel();
  doc["ap_hidden"] = ConfigStorage::getAPHidden();
  doc["ap_status"] = ConfigStorage::getAPStatus();
  doc["hostname"] = ConfigStorage::getHostname();

  sendJson(request, doc);
}

void WebServerManager::handlePostSettings(AsyncWebServerRequest *request, const JsonObject &json)
{
  if (refuseWhileUnconfirmed(request))
    return;

  const char *required[] = {"ap_ssid", "ap_password", "ap_channel", "ap_hidden", "ap_status", "hostname"};
  String missing;
  for (const char *key : required)
    if (json[key].isNull())
      missing += String(key) + ", ";

  if (!missing.isEmpty())
  {
    missing = missing.substring(0, missing.length() - 2);
    JsonDocument errDoc;
    errDoc["success"] = false;
    errDoc["message"] = "Missing required fields";
    errDoc["fields"] = missing;
    sendJson(request, errDoc, 400);
    return;
  }

  ConfigStorage::saveSettings(
      json["ap_ssid"], json["ap_password"],
      json["ap_channel"], json["ap_hidden"],
      json["ap_status"], json["hostname"]);

  Serial.println("------- New Settings -------");
  Serial.printf("Hostname: %s\n", json["hostname"].as<const char *>());
  Serial.printf("AP SSID: %s\n", json["ap_ssid"].as<const char *>());
  Serial.printf("AP Channel: %d\n", json["ap_channel"].as<int>());
  Serial.printf("AP Hidden: %s\n", json["ap_hidden"].as<bool>() ? "Yes" : "No");
  Serial.printf("AP Status: %s\n", json["ap_status"].as<bool>() ? "Enabled" : "Disabled");
  Serial.println("-----------------------------");

  sendMessage(request, "Settings updated");
  scheduleRestart();
}

void WebServerManager::handleReset(AsyncWebServerRequest *request)
{
  if (refuseWhileUnconfirmed(request))
    return;
  ConfigStorage::factoryReset();
  sendMessage(request, "Resetting to factory settings...");
  scheduleRestart();
}

void WebServerManager::handleReboot(AsyncWebServerRequest *request)
{
  if (refuseWhileUnconfirmed(request))
    return;
  sendMessage(request, "Device is rebooting...");
  scheduleRestart();
}

/**
 * @brief Per-request state of a manual upload, freed by the server with the request.
 */
struct UploadContext
{
  bool owner;
  int code;
  char reason[24];
  char message[160];
};

static void rejectUpload(UploadContext *ctx, int code, const String &reason, const String &message)
{
  ctx->owner = false;
  ctx->code = code;
  strlcpy(ctx->reason, reason.c_str(), sizeof(ctx->reason));
  strlcpy(ctx->message, message.c_str(), sizeof(ctx->message));
}

void WebServerManager::registerUpdateRoutes()
{
  // Longer paths first: server.on("/api/update") also matches "/api/update/..."
  server.on("/api/update/status", HTTP_GET, [this](AsyncWebServerRequest *request)
            {
    JsonDocument doc;
    updateManager.fillStatus(doc.to<JsonObject>());
    sendJson(request, doc); });

  server.on("/api/update/check", HTTP_POST, [this](AsyncWebServerRequest *request)
            {
    String message;
    int code = updateManager.requestCheck(message);
    sendMessage(request, message, code); });

  server.on("/api/update/install", HTTP_POST, [this](AsyncWebServerRequest *request)
            {
    String message;
    int code = updateManager.requestInstall(message);
    sendMessage(request, message, code); });

  server.on("/api/update/rollback", HTTP_POST, [this](AsyncWebServerRequest *request)
            {
    String message;
    int code = updateManager.requestRollback(message);
    sendMessage(request, message, code);
    if (code == 200)
      scheduleRestart(1000); });

  server.on(
      "/api/update", HTTP_POST,
      [this](AsyncWebServerRequest *request)
      { handleUploadDone(request); },
      nullptr,
      [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
      { handleUploadBody(request, data, len, index, total); });
}

void WebServerManager::handleUploadBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  if (index == 0)
  {
    UploadContext *ctx = (UploadContext *)calloc(1, sizeof(UploadContext));
    if (!ctx)
      return;
    request->_tempObject = ctx;

    // The middleware only runs once the body has arrived: check before writing to flash
    String reason, message;
    if (!request->contentType().equalsIgnoreCase("application/octet-stream"))
    {
      rejectUpload(ctx, 415, "invalid_file", "Send the update as application/octet-stream");
      return;
    }
    if (!isTrustedPost(request, reason))
    {
      rejectUpload(ctx, 403, "forbidden", reason);
      return;
    }

    UpdateManager::UploadOptions options;
    options.allowDowngrade = request->hasParam("allow_downgrade");
    options.allowTargetChange = request->hasParam("allow_target_change");
    if (!updateManager.uploadBegin(total, data, len, options, reason, message))
    {
      rejectUpload(ctx, reason == "update_running" || reason == "finishing_update" ? 409 : 400, reason, message);
      return;
    }
    ctx->owner = true;
    request->onDisconnect([]()
                          { updateManager.uploadCancel(); });
  }

  UploadContext *ctx = (UploadContext *)request->_tempObject;
  if (!ctx || !ctx->owner)
    return;

  if (!updateManager.uploadWrite(data, len, index))
  {
    rejectUpload(ctx, 400, updateManager.lastReason(), updateManager.lastMessage());
    return;
  }

  if (index + len == total)
  {
    if (updateManager.uploadFinish())
      ctx->code = 200;
    else
      rejectUpload(ctx, 400, updateManager.lastReason(), updateManager.lastMessage());
  }
}

void WebServerManager::handleUploadDone(AsyncWebServerRequest *request)
{
  UploadContext *ctx = (UploadContext *)request->_tempObject;
  if (!ctx)
  {
    sendMessage(request, "Send the update file as the request body", 411);
    return;
  }
  if (ctx->code == 200)
  {
    sendMessage(request, "Update installed, rebooting");
    scheduleRestart(1000);
    return;
  }

  JsonDocument doc;
  doc["success"] = false;
  doc["error"] = ctx->reason;
  doc["message"] = ctx->message;
  Serial.println(ctx->message);
  sendJson(request, doc, ctx->code ? ctx->code : 400);
}
