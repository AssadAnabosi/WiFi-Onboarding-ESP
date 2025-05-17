#include <ESPAsyncWebServer.h>
#include "WebServerManager.h"
#include "WiFiManager.h"
#include "ConfigStorage.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#ifdef ESP32
#include <AsyncTCP.h>
#else
#include <ESPAsyncTCP.h>
#endif

/**
 * @warning //Must always come after ESPAsyncWebServer.h to resolve potential type declaration issues.
 **/
#include "AsyncJson.h"

#if defined(ESP32)
#define PLATFORM "ESP32"
#elif defined(ESP8266)
#define PLATFORM "ESP8266"
#else
#define PLATFORM "Unknown"
#endif

WebServerManager::WebServerManager()
  : server(80) {}

void WebServerManager::begin() {
  // Serve Pages
  server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
    cachedResponse(request, "/www/index.html");
  });
  server.on("/home", HTTP_GET, [this](AsyncWebServerRequest *request) {
    cachedResponse(request, "/www/home.html");
  });
  server.on("/scan", HTTP_GET, [this](AsyncWebServerRequest *request) {
    cachedResponse(request, "/www/scan.html");
  });
  server.on("/settings", HTTP_GET, [this](AsyncWebServerRequest *request) {
    cachedResponse(request, "/www/settings.html");
  });

  // Serve static files
  server.serveStatic("/css/", LittleFS, "/www/css/", "max-age=3600");
  server.serveStatic("/js/", LittleFS, "/www/js/", "max-age=3600");

  // GET API routes
  server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
    handleStatus(request);
  });
  server.on("/api/scan", HTTP_GET, [this](AsyncWebServerRequest *request) {
    handleScan(request);
  });
  server.on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest *request) {
    handleGetSettings(request);
  });

  // POST API routes with AsyncJson
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/connect", [this](AsyncWebServerRequest *request, JsonVariant &json) {
    handleConnect(request, json.as<JsonObject>());
  }));

  server.on("/api/disconnect", HTTP_POST, [this](AsyncWebServerRequest *request) {
    handleDisconnect(request);
  });

  server.addHandler(new AsyncCallbackJsonWebHandler("/api/settings", [this](AsyncWebServerRequest *request, JsonVariant &json) {
    handlePostSettings(request, json.as<JsonObject>());
  }));

  server.on("/api/reboot", HTTP_POST, [this](AsyncWebServerRequest *request) {
    handleReboot(request);
  });

  server.on("/api/reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
    handleReset(request);
  });

  server.on("/api", HTTP_GET, [this](AsyncWebServerRequest *request) {
    handleHealth(request);
  });
  // 404 fallback
  server.onNotFound([this](AsyncWebServerRequest *request) {
    sendMessage(request, "Not Found", 404);
  });

  server.begin();
  Serial.println("Web Server Started");
}

void WebServerManager::sendJson(AsyncWebServerRequest *request, const DynamicJsonDocument &doc, int code = 200) {
  String response;
  serializeJson(doc, response);
  request->send(code, "application/json", response);
}

void WebServerManager::sendMessage(AsyncWebServerRequest *request, const String &message, int code = 200) {
  DynamicJsonDocument doc(128);
  doc["success"] = (code == 200);
  doc["message"] = message;
  Serial.println(message);
  sendJson(request, doc, code);
}

void WebServerManager::cachedResponse(AsyncWebServerRequest *request, const String &path) {
  // Validate if path exists
  if (!LittleFS.exists(path)) {
    sendMessage(request, "File not found", 404);
    return;
  }
  AsyncWebServerResponse *response = request->beginResponse(LittleFS, path, "text/html");
  response->addHeader("Cache-Control", "max-age=3600");
  request->send(response);
}

/**
 * @brief Converts the encryption type to a human-readable string.
 * @param encType The encryption type [received from WiFi.scan] as an integer.
 * @return A string representing the encryption type.
 */

String getEncryptionType(int encType) {
#ifdef ESP32
  switch (encType) {
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
#else
  switch (encType) {
    case ENC_TYPE_NONE:
      return "OPEN";
    case ENC_TYPE_WEP:
      return "WEP";
    case ENC_TYPE_TKIP:
      return "WPA-PSK";
    case ENC_TYPE_CCMP:
      return "WPA2-PSK";
    case ENC_TYPE_AUTO:
      return "AUTO";
    default:
      return "UNKNOWN";
  }

#endif
}

/**
 * @brief Populates the JSON object with board data.
 * @param output The JSON object to populate.
 */
void getBoardData(JsonObject &output) {
#ifdef ESP32
  // Platform
  output["platform"] = "ESP32";

  // MAC Address
  output["mac"] = WiFi.macAddress();

  // Chip Info
  JsonObject chipInfo = output.createNestedObject("chip_info");
  chipInfo["chip_model"] = ESP.getChipModel();
  chipInfo["chip_revision"] = ESP.getChipRevision();
  chipInfo["cores"] = ESP.getChipCores();
  chipInfo["cpu_freq_mhz"] = ESP.getCpuFreqMHz();

  // Memory
  JsonObject memory = output.createNestedObject("memory");
  memory["flash_size_mb"] = ESP.getFlashChipSize() / (1024 * 1024);
  memory["free_heap_kb"] = ESP.getFreeHeap() / 1024;

  // SDK/Arduino versions
  JsonObject software = output.createNestedObject("software");
  software["sdk_version"] = ESP.getSdkVersion();
  software["arduino_core"] = ESP.getCoreVersion();
#else
  output["platform"] = "ESP8266";
  JsonObject chipInfo = output.createNestedObject("chip_info");
  chipInfo["chip_model"] = "ESP8266";
  chipInfo["chip_id"] = ESP.getChipId();

  JsonObject memory = output.createNestedObject("memory");
  output["flash_size_mb"] = ESP.getFlashChipRealSize() / (1024 * 1024);
  output["free_heap_kb"] = ESP.getFreeHeap() / 1024;

  JsonObject software = output.createNestedObject("software");
  software["sdk_version"] = ESP.getCoreVersion();
  software["mac"] = WiFi.macAddress();
#endif
}

void WebServerManager::handleHealth(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(512);

  doc["success"] = true;
  doc["message"] = "What are you doing here ?";

  // Add nested "data" object with board info
  JsonObject data = doc.createNestedObject("data");
  getBoardData(data); // Populate dynamically

  sendJson(request, doc);
}

void WebServerManager::handleStatus(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(512);

  bool apEnabled = WiFi.getMode() & WIFI_AP;
  bool staConnected = (WiFi.status() == WL_CONNECTED);

  doc["softAP"]["enabled"] = apEnabled;
  doc["softAP"]["ssid"] = WiFi.softAPSSID();
  doc["softAP"]["ip"] = apEnabled ? WiFi.softAPIP().toString() : "";

  doc["station"]["connected"] = staConnected;
  doc["station"]["ssid"] = WiFi.SSID();
  doc["station"]["ip"] = staConnected ? WiFi.localIP().toString() : "";

  sendJson(request, doc);
}

void WebServerManager::handleScan(AsyncWebServerRequest *request) {
  int n = WiFi.scanNetworks(false, true);
  bool connected = (WiFi.status() == WL_CONNECTED);
  String currentSSID = WiFi.SSID();

  int indices[n];
  for (int i = 0; i < n; ++i)
    indices[i] = i;

  for (int i = 0; i < n - 1; ++i) {
    for (int j = i + 1; j < n; ++j) {
      if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i])) {
        int temp = indices[i];
        indices[i] = indices[j];
        indices[j] = temp;
      }
    }
  }

  DynamicJsonDocument doc(2048);
  JsonArray networks = doc.to<JsonArray>();

  for (int i = 0; i < n; ++i) {
    JsonObject network = networks.createNestedObject();
    network["ssid"] = WiFi.SSID(i).c_str();
    network["rssi"] = WiFi.RSSI(i);
    network["enc"] = getEncryptionType(WiFi.encryptionType(i));
    network["mac"] = WiFi.BSSIDstr(i).c_str();
    network["channel"] = WiFi.channel(i);
    network["connected"] = connected && (currentSSID.equals(WiFi.SSID(i)));
  }

  WiFi.scanDelete();
  sendJson(request, doc);
}

void WebServerManager::handleConnect(AsyncWebServerRequest *request, const JsonObject &json) {
  if (!json.containsKey("ssid")) {
    sendMessage(request, "Missing required field: ssid", 400);
    return;
  }

  String ssid = json["ssid"].as<String>();
  String password = json["password"] | "";

  if (wifiManager.connectToNetwork(ssid.c_str(), password.c_str()))
    sendMessage(request, "Connected to " + String(ssid) + " WiFi Network");
  else
    sendMessage(request, "Failed to connect to WiFi Network", 400);
}

void WebServerManager::handleDisconnect(AsyncWebServerRequest *request) {
  WiFi.disconnect(true);
  ConfigStorage::saveWiFiCredentials("", "");
  sendMessage(request, "Disconnected from WiFi Network");
}

void WebServerManager::handleGetSettings(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(512);

  doc["ap_ssid"] = ConfigStorage::readAPSSID();
  doc["ap_password"] = ConfigStorage::readAPPassword();
  doc["ap_channel"] = ConfigStorage::readAPChannel();
  doc["ap_hidden"] = ConfigStorage::readAPHidden();
  doc["ap_status"] = ConfigStorage::readAPStatus();
  doc["hostname"] = ConfigStorage::readHostname();

  sendJson(request, doc);
}

void WebServerManager::handlePostSettings(AsyncWebServerRequest *request, const JsonObject &json) {
  const char *required[] = { "ap_ssid", "ap_password", "ap_channel", "ap_hidden", "ap_status", "hostname" };
  String missing;
  for (const char *key : required)
    if (!json.containsKey(key))
      missing += String(key) + ", ";

  if (!missing.isEmpty()) {
    missing = missing.substring(0, missing.length() - 2);
    DynamicJsonDocument errDoc(256);
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
  Serial.printf("AP Password: %s\n", json["ap_password"].as<const char *>());
  Serial.printf("AP Channel: %d\n", json["ap_channel"].as<int>());
  Serial.printf("AP Hidden: %s\n", json["ap_hidden"].as<bool>() ? "Yes" : "No");
  Serial.printf("AP Status: %s\n", json["ap_status"].as<bool>() ? "Enabled" : "Disabled");
  Serial.println("-----------------------------");

  sendMessage(request, "Settings updated");
  delay(100);
  ESP.restart();
}

void WebServerManager::handleReset(AsyncWebServerRequest *request) {
  sendMessage(request, "Resetting to factory settings...");
  delay(100);
  ConfigStorage::factoryReset();
  delay(100);
  ESP.restart();
}

void WebServerManager::handleReboot(AsyncWebServerRequest *request) {
  sendMessage(request, "Device is rebooting...");
  delay(100);
  ESP.restart();
}
