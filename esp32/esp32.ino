#include "ProjectConfig.h"
#include <ESPmDNS.h>

#include "WebServerManager.h"
#include "ConfigStorage.h"

#include "WiFiManager.h"
#include "UpdateManager.h"

WiFiManager wifiManager;

WebServerManager webServer;

void setup() {
  Serial.begin(115200);
  // Flush serial buffer
  Serial.println("");
  ConfigStorage::begin();
  // Initialize WiFiManager and attempt to connect to WiFi
  wifiManager.autoConnect();
  updateManager.begin();
  webServer.begin();
  if (MDNS.begin(MDNS_SETUP_NAME))  // "<name>.local" resolves to the SoftAP IP
  {
    Serial.println("mDNS responder started: " MDNS_SETUP_NAME ".local");
    MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println("Error starting mDNS responder");
  }
  updateManager.setupDone();
}

void loop() {
  updateManager.loop();
  webServer.loop();
}
