#include "WiFiManager.h"
#include "ConfigStorage.h"

#define AP_IP IPAddress(192, 168, 4, 1)
#define AP_GATEWAY IPAddress(192, 168, 4, 1)
#define AP_SUBNET_MASK IPAddress(255, 255, 255, 0)

void WiFiManager::setupSoftAP()
{
  Serial.println("Setting up SoftAP...");
  WiFi.mode(WIFI_AP_STA);
  String APSSID = ConfigStorage::getAPSSID();
  String APPassword = ConfigStorage::getAPPassword();
  int APChannel = ConfigStorage::getAPChannel();
  bool APHidden = ConfigStorage::getAPHidden();

  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET_MASK);
  WiFi.softAP(APSSID.c_str(), APPassword.c_str(), APChannel, APHidden ? 1 : 0);
  Serial.print("AP SSID: ");
  Serial.println(WiFi.softAPSSID());
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
}

bool WiFiManager::autoConnect()
{
  WiFi.disconnect(true);

  String hostname = ConfigStorage::getHostname();
  WiFi.hostname(hostname);

  String ssid = ConfigStorage::getWiFiSSID();
  String password = ConfigStorage::getWiFiPassword();

  WiFi.mode(WIFI_STA);
  int retryCount = 0;
  if (ssid.length() != 0)
  {
    Serial.println("Attempting to connect to saved WiFi credentials");
    connectToNetwork(ssid.c_str(), password.c_str());
  }

  if (ConfigStorage::getAPStatus() || !(WiFi.status() == WL_CONNECTED))
  {
    // Override the Disabled AP setting if the auto-connect fails
    setupSoftAP();
  }
  return WiFi.status() == WL_CONNECTED;
}

bool WiFiManager::connectToNetwork(const char *ssid, const char *password)
{
  WiFi.disconnect();
  WiFi.begin(ssid, password, true);
  Serial.print("Connecting to " + String(ssid) + ".");

  int retryCount = 0;

  while (WiFi.status() != WL_CONNECTED && retryCount < 100)
  {
    if (!(retryCount % 10))
      Serial.print(".");
    delay(100);
    retryCount++;
  }
  Serial.println("");
  if (WiFi.status() == WL_CONNECTED)
  {
    ConfigStorage::saveWiFiCredentials(ssid, password);
    Serial.println("Connected to " + String(ssid) + " WiFi Network Successfully");
    Serial.print("STA IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  }
  else
  {
    WiFi.disconnect();
    Serial.println("Failed to connect to " + String(ssid) + " WiFi Network");
    return false;
  }
}