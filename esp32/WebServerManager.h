#pragma once
#include <WiFi.h>

#include <ESPAsyncWebServer.h>
#include "WiFiManager.h"
#include <ArduinoJson.h>

struct WebAsset;

extern WiFiManager wifiManager;

/**
 * @brief Manages the web server functionality for device control and configuration.
 */
class WebServerManager {
public:
  /**
     * @brief Constructs a WebServerManager instance.
     */
  WebServerManager();

  /**
     * @brief Initializes the web server and sets up the endpoints.
     */
  void begin();

  /**
     * @brief Runs deferred work (such as a scheduled restart). Call from loop().
     */
  void loop();

  /**
     * @brief Sends a JSON document as an HTTP response.
     * @param request The HTTP request object.
     * @param doc The JSON document to send.
     * @param code The HTTP status code to send (default is 200).
     */
  void sendJson(AsyncWebServerRequest *request, const JsonDocument &doc, int code = 200);

  /**
     * @brief Sends a message as a JSON document as an HTTP response.
     * @param request The HTTP request object.
     * @param message The message to send.
     * @param code The HTTP status code to send (default is 200). Any 2xx code is reported as success.
     */
  void sendMessage(AsyncWebServerRequest *request, const String &message, int code = 200);

  /**
     * @brief Serves an embedded, gzipped web file, answering 304 when the browser's copy is current.
     * @param request The HTTP request object.
     * @param asset The embedded file.
     */
  void serveAsset(AsyncWebServerRequest *request, const WebAsset *asset);

  /**
     * @brief Liveness check: confirms the server is running and returns board info.
     * @param request The HTTP request object.
     */
  void handleHealth(AsyncWebServerRequest *request);

  /**
     * @brief JSON response of current connections status, and ips.
     * @param request The HTTP request object.
     */
  void handleStatus(AsyncWebServerRequest *request);

  /**
     * @brief Returns the networks found by the background scan, starting one if needed.
     * Answers 202 while the scan is still running.
     * @param request The HTTP request object.
     */
  void handleScan(AsyncWebServerRequest *request);

  /**
     * @brief Starts connecting the device to a WiFi network in the background. Answers 202.
     * The result is reported by /api/status.
     * @param request The HTTP request object.
     */
  void handleConnect(AsyncWebServerRequest *request, const JsonObject &json);

  /**
     * @brief Handles a request to disconnect the device from WiFi.
     * @param request The HTTP request object.
     */
  void handleDisconnect(AsyncWebServerRequest *request);

  /**
     * @brief JSON response of current device settings.
     * @param request The HTTP request object.
     */
  void handleGetSettings(AsyncWebServerRequest *request);

  /**
     * @brief Handles a request to update device settings.
     * @param request The HTTP request object.
     */
  void handlePostSettings(AsyncWebServerRequest *request, const JsonObject &json);

  /**
     * @brief Handles a request to reboot the device.
     * @param request The HTTP request object.
     */
  void handleReboot(AsyncWebServerRequest *request);

  /**
     * @brief Handles a request to reset the device to factory settings.
     * @param request The HTTP request object.
     */
  void handleReset(AsyncWebServerRequest *request);

private:
  AsyncWebServer server;
  uint32_t _restartAt = 0;
  bool _restartPending = false;
  uint32_t _scanStartedAt = 0;

  /**
     * @brief Restarts the device from loop() once the response has had time to be sent.
     * @param delayMs How long to wait before restarting.
     */
  void scheduleRestart(uint32_t delayMs = 500);

  /**
     * @brief Cross-site request protection for POST routes.
     * Accepts only JSON or raw binary bodies, a Host that is one of the device's own names,
     * and (when present) an Origin pointing at the device's own pages.
     * @param request The HTTP request object.
     * @param reason Set to the rejection reason when the request is refused.
     * @return True if the request may be handled.
     */
  bool isTrustedPost(AsyncWebServerRequest *request, String &reason);

  /**
     * @brief Checks whether a host name (without port) is one of the device's own names.
     */
  bool isOwnHost(String host);

  /**
     * @brief Registers every update route. All update routes go through this one function.
     */
  void registerUpdateRoutes();

  /**
     * @brief Receives a manual upload (raw application/octet-stream body) chunk by chunk.
     */
  void handleUploadBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

  /**
     * @brief Answers a manual upload once its body has been received.
     */
  void handleUploadDone(AsyncWebServerRequest *request);
};
