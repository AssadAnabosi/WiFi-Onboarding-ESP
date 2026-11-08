// Wi-Fi onboarding with signed OTA updates. Module overview: docs/architecture.md.
// This file creates the modules and wires them together; the code lives in src/.

#include "src/ota/FirmwareSlots.h"
#include "src/ota/UpdateManager.h"
#include "src/storage/ConfigStorage.h"
#include "src/system/Restart.h"
#include "src/web/WebServerManager.h"
#include "src/wifi/WiFiManager.h"

FirmwareSlots firmwareSlots;
UpdateManager updateManager(firmwareSlots);
WiFiManager wifiManager;
WebServerManager webServer(wifiManager, updateManager, firmwareSlots);

void setup()
{
  Serial.begin(115200);
  Serial.println();

  firmwareSlots.begin(); // First, so the rollback deadline of new firmware is armed early
  ConfigStorage::logSettings();
  wifiManager.begin();
  updateManager.begin();
  webServer.begin();
  firmwareSlots.setupDone();
}

void loop()
{
  firmwareSlots.loop();
  updateManager.loop();
  Restart::loop();
}
