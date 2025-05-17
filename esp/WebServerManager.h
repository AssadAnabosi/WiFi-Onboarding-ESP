#pragma once
#ifdef ESP32
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif

#include <ESPAsyncWebServer.h>
#include "WiFiManager.h"
#include <ArduinoJson.h>

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
     * @brief Sends a JSON document as an HTTP response.
     * @param request The HTTP request object.
     * @param doc The JSON document to send.
     * @param code The HTTP status code to send (default is 200).
     */
  void sendJson(AsyncWebServerRequest *request, const DynamicJsonDocument &doc, int code);

  /**
     * @brief Sends a message as a JSON document as an HTTP response.
     * @param request The HTTP request object.
     * @param message The message to send.
     * @param code The HTTP status code to send (default is 200).
     */
  void sendMessage(AsyncWebServerRequest *request, const String &message, int code);

  /**
     * @brief Utility function to serve static files with cache control.
     * @param uri The URI of the static file.
     * @param path The filesystem path of the static file.
     */
  void cachedResponse(AsyncWebServerRequest *request, const String &path);

  /**
     * @brief  Sends back OK response indicating the server is running.
     * @param request The HTTP request object.
     */
  void handleHealth(AsyncWebServerRequest *request);

  /**
     * @brief JSON response of current connections status, and ips.
     * @param request The HTTP request object.
     */
  void handleStatus(AsyncWebServerRequest *request);

  /**
     * @brief Scan for available WiFi networks and returns the JSON response.
     * @param request The HTTP request object.
     * @param n The number of networks found.
     */
  void handleScan(AsyncWebServerRequest *request);

  /**
     * @brief Handles a request to connect the device to a WiFi network.
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
};
