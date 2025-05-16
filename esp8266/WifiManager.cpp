#include "WiFiManager.h"
#include "ConfigStorage.h"

#define AP_IP IPAddress(192, 168, 4, 1)
#define AP_SUBNET_MASK IPAddress(255, 255, 255, 0)

void WiFiManager::setupSoftAP()
{
  Serial.println("Setting up SoftAP...");
  WiFi.mode(WIFI_AP_STA);
  String APSSID = ConfigStorage::readAPSSID();
  String APPassword = ConfigStorage::readAPPassword();
  int APChannel = ConfigStorage::readAPChannel();
  bool APHidden = ConfigStorage::readAPHidden();

  WiFi.softAPConfig(AP_IP, AP_IP, AP_SUBNET_MASK);
  WiFi.softAP(APSSID.c_str(), APPassword.c_str(), APChannel, APHidden ? 1 : 0);
  Serial.print("AP SSID: ");
  Serial.println(WiFi.softAPSSID());
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
}

bool WiFiManager::autoConnect()
{
  String ssid = ConfigStorage::readWiFiSSID();
  String password = ConfigStorage::readWiFiPassword();
  String hostname = ConfigStorage::readHostname();

  WiFi.mode(WIFI_STA);

  if (ssid.length() != 0)
  {
    Serial.println("Attempting to Auto Connect to " + ssid + "...");
    WiFi.hostname(hostname);
    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long startAttemptTime = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000)
    {
      delay(100);
    }
  }

  if (ConfigStorage::readAPStatus() || !(WiFi.status() == WL_CONNECTED))
  {
    // Override the Disabled AP setting if the auto-connect fails
    setupSoftAP();
  }
  return WiFi.status() == WL_CONNECTED;
}

bool WiFiManager::connectToNetwork(const char *ssid, const char *password)
{
  String hostname = ConfigStorage::readHostname();
  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);

  setupSoftAP();

  WiFi.hostname(hostname);
  WiFi.begin(ssid, password);

  unsigned long startAttemptTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000)
  {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    ConfigStorage::saveWiFiCredentials(ssid, password);
    return true;
  }
  else
  {
    return false;
  }
}