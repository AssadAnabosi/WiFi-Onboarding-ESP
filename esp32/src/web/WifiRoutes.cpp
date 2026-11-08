// /api/scan, /api/connect, /api/disconnect

#include "WebServerManager.h"

#include <AsyncJson.h> // Must come after ESPAsyncWebServer.h
#include <vector>

#include "../ota/UpdateManager.h"
#include "../wifi/WiFiManager.h"

void WebServerManager::registerWifiRoutes()
{
  _server.on("/api/scan", HTTP_GET, [this](AsyncWebServerRequest *request)
             { handleScan(request); });
  _server.addHandler(new AsyncCallbackJsonWebHandler("/api/connect", [this](AsyncWebServerRequest *request, JsonVariant &json)
                                                     { handleConnect(request, json.as<JsonObject>()); }));
  _server.on("/api/disconnect", HTTP_POST, [this](AsyncWebServerRequest *request)
             { handleDisconnect(request); });
}

void WebServerManager::handleScan(AsyncWebServerRequest *request)
{
  if (_updates.isBusy())
  {
    sendMessage(request, "An update is running, try again later", 409);
    return;
  }

  std::vector<WiFiManager::Network> networks;
  switch (_wifi.scan(networks))
  {
  case WiFiManager::ScanState::Scanning:
    sendMessage(request, "Scanning", 202);
    return;
  case WiFiManager::ScanState::Busy:
    sendMessage(request, "Connecting to a network, try again shortly", 409);
    return;
  case WiFiManager::ScanState::Failed:
    sendMessage(request, "Failed to start WiFi scan", 500);
    return;
  case WiFiManager::ScanState::Ready:
    break;
  }

  JsonDocument doc;
  JsonArray list = doc.to<JsonArray>();
  for (const WiFiManager::Network &network : networks)
  {
    JsonObject item = list.add<JsonObject>();
    item["ssid"] = network.ssid.isEmpty() ? String("Hidden Network") : network.ssid;
    item["hidden"] = network.hidden;
    item["rssi"] = network.rssi;
    item["enc"] = network.encryption;
    item["mac"] = network.bssid;
    item["channel"] = network.channel;
    item["connected"] = network.connected;
  }
  sendJson(request, doc);
}

void WebServerManager::handleConnect(AsyncWebServerRequest *request, JsonObject json)
{
  if (json["ssid"].isNull())
  {
    sendMessage(request, "Missing required field: ssid", 400);
    return;
  }
  if (_updates.isBusy())
  {
    sendMessage(request, "An update is running, try again later", 409);
    return;
  }
  if (_wifi.isScanning())
  {
    sendMessage(request, "WiFi scan in progress, try again shortly", 409);
    return;
  }

  String ssid = json["ssid"].as<String>();
  String password = json["password"] | "";
  if (!_wifi.startConnect(ssid, password))
  {
    if (_wifi.isConnecting())
      sendMessage(request, "Already connecting to a network", 409);
    else
      sendMessage(request, "Failed to start connecting", 500);
    return;
  }
  sendMessage(request, "Connecting to " + ssid, 202);
}

void WebServerManager::handleDisconnect(AsyncWebServerRequest *request)
{
  _wifi.forgetNetwork();
  sendMessage(request, "Disconnected from WiFi Network");
}
