#pragma once

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

class FirmwareSlots;
class UpdateManager;
class WiFiManager;
struct UpdateReply;
struct WebAsset;

/**
 * @brief Serves the embedded web interface and the JSON API (docs/api.md).
 *
 * Every POST passes RequestGuard first. Responses are {success, message} (any 2xx is a
 * success). Routes are registered by area, one file each: SystemRoutes.cpp, WifiRoutes.cpp
 * and UpdateRoutes.cpp. Note that server.on("/a") also matches "/a/...", so longer routes
 * are registered first.
 */
class WebServerManager
{
public:
  WebServerManager(WiFiManager &wifi, UpdateManager &updates, FirmwareSlots &slots);

  /**
   * @brief Registers every route and starts the server. Call from setup().
   */
  void begin();

private:
  AsyncWebServer _server;
  WiFiManager &_wifi;
  UpdateManager &_updates;
  FirmwareSlots &_slots;

  // Cross-site protection and responses (WebServerManager.cpp)
  static void guardPost(AsyncWebServerRequest *request, ArMiddlewareNext next);
  static void sendJson(AsyncWebServerRequest *request, const JsonDocument &doc, int code = 200);
  static void sendMessage(AsyncWebServerRequest *request, const String &message, int code = 200);
  static void sendMessage(AsyncWebServerRequest *request, const UpdateReply &reply);
  static void sendFailure(AsyncWebServerRequest *request, const UpdateReply &reply);
  static void serveAsset(AsyncWebServerRequest *request, const WebAsset *asset);

  /**
   * @brief Answers 409 "Finishing update" while new firmware is unconfirmed.
   * @return True if the request was refused.
   */
  bool refuseWhileUnconfirmed(AsyncWebServerRequest *request);

  // /api/health, /api/status, /api/settings, /api/reboot, /api/reset (SystemRoutes.cpp)
  void registerSystemRoutes();
  void handleHealth(AsyncWebServerRequest *request);
  void handleStatus(AsyncWebServerRequest *request);
  void handleGetSettings(AsyncWebServerRequest *request);
  void handlePostSettings(AsyncWebServerRequest *request, JsonObject json);
  void handleReboot(AsyncWebServerRequest *request);
  void handleReset(AsyncWebServerRequest *request);

  // /api/scan, /api/connect, /api/disconnect (WifiRoutes.cpp)
  void registerWifiRoutes();
  void handleScan(AsyncWebServerRequest *request);
  void handleConnect(AsyncWebServerRequest *request, JsonObject json);
  void handleDisconnect(AsyncWebServerRequest *request);

  // /api/update/... (UpdateRoutes.cpp)
  void registerUpdateRoutes();
  void handleUpdateStatus(AsyncWebServerRequest *request);
  void handleRollback(AsyncWebServerRequest *request);
  void handleUploadBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
  void handleUploadDone(AsyncWebServerRequest *request);
};
