#define PLATFORM "ESP8266"
#include <ESP8266mDNS.h>

#include "WebServerManager.h"
#include "ConfigStorage.h"

#include "WiFiManager.h"

WiFiManager wifiManager;

WebServerManager webServer;

void setup() {
  Serial.begin(115200);
  // Flush serial buffer
  Serial.println("");
  Serial.print("Running on platform: ");
  Serial.println(PLATFORM);
  ConfigStorage::begin();
  // Initialize WiFiManager and attempt to connect to WiFi
  wifiManager.autoConnect();
  webServer.begin();
  if (MDNS.begin("config"))  // "config.local" will resolve to the SoftAP IP
  {
    Serial.println("mDNS responder started: config.local");
    MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println("Error starting mDNS responder");
  }
}

void loop() {
#ifdef ESP8266
  MDNS.update();
#endif
}
