#include "WiFiManager.h"
#include "ConfigStorage.h"

#ifdef ESP32
#include "esp_wpa2.h" // WPA2 Enterprise support (ESP32)
#endif

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
  if (ssid.length() != 0)
  {
    for (int attempt = 1; attempt <= 3; attempt++)
    {
      Serial.println("[" + String(attempt) + "] Attempting to connect to saved WiFi credentials");
      if (connectToNetwork(ssid.c_str(), password.c_str()))
        break;
    }
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

  // Support for enterprise credentials stored in the password field using
  // the format: "username|password". If the stored password contains a
  // pipe ('|') we treat it as WPA2-Enterprise (PEAP/MSCHAPv2) credentials
  // where the left side is the identity/username and the right side is the
  // actual password. This keeps the stored structure unchanged while
  // supporting both enterprise and non-enterprise networks.
  String pwd = String(password);
  bool isEnterprise = pwd.indexOf('|') >= 0;
  if (isEnterprise)
  {
#ifdef ESP32
    int sep = pwd.indexOf('|');
    String username = pwd.substring(0, sep);
    String userpass = pwd.substring(sep + 1);

    // Configure WPA2 Enterprise
    esp_wpa2_config_t conf = WPA2_CONFIG_INIT_DEFAULT();
    esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)username.c_str(), username.length());
    esp_wifi_sta_wpa2_ent_set_username((uint8_t *)username.c_str(), username.length());
    esp_wifi_sta_wpa2_ent_set_password((uint8_t *)userpass.c_str(), userpass.length());
    esp_wifi_sta_wpa2_ent_enable();

    WiFi.begin(ssid);
#else
    Serial.println("WPA2-Enterprise requested but not supported on this platform. Falling back to PSK.");
    WiFi.begin(ssid, password);
#endif
  }
  else
  {
    WiFi.begin(ssid, password);
  }
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