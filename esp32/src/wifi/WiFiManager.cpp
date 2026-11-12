#include "WiFiManager.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <algorithm>
#include <esp_eap_client.h> // WPA2-Enterprise

#include "../../ProjectConfig.h"
#include "../storage/ConfigStorage.h"
#include "../system/Log.h"

namespace
{
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_SUBNET_MASK(255, 255, 255, 0);

constexpr int SAVED_NETWORK_ATTEMPTS = 3;
constexpr uint32_t CONNECT_TIMEOUT_MS = 10000;
constexpr uint32_t CONNECT_TASK_STACK = 8192;

// Scan results older than this are discarded and a fresh scan is started
constexpr uint32_t SCAN_RESULTS_MAX_AGE_MS = 15000;

const char *encryptionName(wifi_auth_mode_t type)
{
  switch (type)
  {
  case WIFI_AUTH_OPEN:
    return "OPEN";
  case WIFI_AUTH_WEP:
    return "WEP";
  case WIFI_AUTH_WPA_PSK:
    return "WPA-PSK";
  case WIFI_AUTH_WPA2_PSK:
    return "WPA2-PSK";
  case WIFI_AUTH_WPA_WPA2_PSK:
    return "WPA/WPA2-PSK";
  case WIFI_AUTH_WPA2_ENTERPRISE:
    return "WPA2-ENTERPRISE";
  case WIFI_AUTH_WPA3_PSK:
    return "WPA3-PSK";
  case WIFI_AUTH_WPA2_WPA3_PSK:
    return "WPA2/WPA3-PSK";
  default:
    return "UNKNOWN";
  }
}

// Enterprise credentials are stored as "username|password" (PEAP/MSCHAPv2), so the
// stored format stays the same for every network. Enterprise mode stays enabled in
// the driver until turned off, so it is set for every connection.
void configureEnterprise(const String &password, bool &isEnterprise)
{
  int separator = password.indexOf('|');
  isEnterprise = separator >= 0;
  if (!isEnterprise)
  {
    esp_wifi_sta_enterprise_disable();
    return;
  }
  String username = password.substring(0, separator);
  String userPassword = password.substring(separator + 1);
  esp_eap_client_set_identity((const unsigned char *)username.c_str(), username.length());
  esp_eap_client_set_username((const unsigned char *)username.c_str(), username.length());
  esp_eap_client_set_password((const unsigned char *)userPassword.c_str(), userPassword.length());
  esp_wifi_sta_enterprise_enable();
}
} // namespace

void WiFiManager::begin()
{
  // Credentials live in ConfigStorage; stop the driver keeping its own copy
  WiFi.persistent(false);
  WiFi.disconnect(true);

  DeviceSettings settings = ConfigStorage::loadSettings();
  WiFi.hostname(settings.hostname);
  WiFi.mode(WIFI_STA);

  String ssid = ConfigStorage::getWiFiSSID();
  if (!ssid.isEmpty())
  {
    String password = ConfigStorage::getWiFiPassword();
    for (int attempt = 1; attempt <= SAVED_NETWORK_ATTEMPTS; attempt++)
    {
      LOG("wifi", "Connecting to the saved network (attempt %d of %d)", attempt, SAVED_NETWORK_ATTEMPTS);
      if (connect(ssid, password))
        break;
    }
  }

  // The access point also starts when it is disabled but no network could be joined,
  // so the device can always be reached
  if (settings.apEnabled || WiFi.status() != WL_CONNECTED)
    startAccessPoint(settings);

  startMdns();
  _ready = true;
}

void WiFiManager::startAccessPoint(const DeviceSettings &settings)
{
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_SUBNET_MASK);
  WiFi.softAP(settings.apSsid.c_str(), settings.apPassword.c_str(), settings.apChannel, settings.apHidden ? 1 : 0);
  LOG("wifi", "Access point %s at %s", WiFi.softAPSSID().c_str(), WiFi.softAPIP().toString().c_str());
}

void WiFiManager::startMdns()
{
  // "<name>.local" resolves to the device on the access point and on the network
  if (!MDNS.begin(MDNS_SETUP_NAME))
  {
    LOG("wifi", "Failed to start mDNS");
    return;
  }
  MDNS.addService("http", "tcp", 80);
  LOG("wifi", "mDNS started: " MDNS_SETUP_NAME ".local");
}

bool WiFiManager::connect(const String &ssid, const String &password)
{
  WiFi.disconnect();

  bool isEnterprise;
  configureEnterprise(password, isEnterprise);
  if (isEnterprise)
    WiFi.begin(ssid.c_str());
  else
    WiFi.begin(ssid.c_str(), password.c_str());
  LOG("wifi", "Connecting to %s%s", ssid.c_str(), isEnterprise ? " (WPA2-Enterprise)" : "");

  uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < CONNECT_TIMEOUT_MS)
    delay(100);

  if (WiFi.status() != WL_CONNECTED)
  {
    WiFi.disconnect();
    LOG("wifi", "Failed to connect to %s", ssid.c_str());
    return false;
  }
  ConfigStorage::saveWiFiCredentials(ssid, password);
  LOG("wifi", "Connected to %s, IP address %s", ssid.c_str(), WiFi.localIP().toString().c_str());
  return true;
}

bool WiFiManager::startConnect(const String &ssid, const String &password)
{
  AttemptState expected = _attemptState.load();
  if (expected == AttemptState::Connecting ||
      !_attemptState.compare_exchange_strong(expected, AttemptState::Connecting))
    return false;

  {
    std::lock_guard<std::mutex> guard(_attemptLock);
    _attemptSsid = ssid;
    _attemptPassword = password;
  }
  if (xTaskCreate(connectTask, "wifi_connect", CONNECT_TASK_STACK, this, 1, nullptr) != pdPASS)
  {
    std::lock_guard<std::mutex> guard(_attemptLock);
    _attemptPassword = "";
    _attemptState = AttemptState::Failed;
    return false;
  }
  return true;
}

void WiFiManager::restoreConnection()
{
  // A failed attempt never replaces the saved network, so this is the last one that worked
  String ssid = ConfigStorage::getWiFiSSID();
  if (!ssid.isEmpty())
  {
    LOG("wifi", "Going back to the saved network");
    if (connect(ssid, ConfigStorage::getWiFiPassword()))
      return;
  }
  // Keep the device reachable when the access point is disabled
  if (!(WiFi.getMode() & WIFI_AP))
    startAccessPoint(ConfigStorage::loadSettings());
}

void WiFiManager::connectTask(void *arg)
{
  WiFiManager *self = static_cast<WiFiManager *>(arg);
  String ssid, password;
  {
    std::lock_guard<std::mutex> guard(self->_attemptLock);
    ssid = self->_attemptSsid;
    password = self->_attemptPassword;
    self->_attemptPassword = "";
  }
  bool connected = self->connect(ssid, password);
  if (!connected)
    self->restoreConnection();
  // Reported only now, so scans and new attempts wait until the fallback has finished
  self->_attemptState = connected ? AttemptState::Succeeded : AttemptState::Failed;
  vTaskDelete(nullptr);
}

WiFiManager::Attempt WiFiManager::attempt() const
{
  std::lock_guard<std::mutex> guard(_attemptLock);
  return {_attemptState.load(), _attemptSsid};
}

void WiFiManager::forgetNetwork()
{
  WiFi.disconnect(true);
  ConfigStorage::saveWiFiCredentials("", "");
}

bool WiFiManager::isScanning() const
{
  return WiFi.scanComplete() == WIFI_SCAN_RUNNING;
}

WiFiManager::ScanState WiFiManager::scan(std::vector<Network> &networks)
{
  std::lock_guard<std::mutex> guard(_scanLock);
  int16_t count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING)
    return ScanState::Scanning;

  // Results nobody collected in time are stale: drop them and scan again
  if (count >= 0 && millis() - _scanStartedAt > SCAN_RESULTS_MAX_AGE_MS)
  {
    WiFi.scanDelete();
    count = WIFI_SCAN_FAILED;
  }

  if (count < 0)
  {
    if (isConnecting())
      return ScanState::Busy;
    if (WiFi.scanNetworks(true, true) == WIFI_SCAN_FAILED)
      return ScanState::Failed;
    _scanStartedAt = millis();
    return ScanState::Scanning;
  }

  bool staConnected = WiFi.status() == WL_CONNECTED;
  String connectedSsid = WiFi.SSID();
  String connectedBssid = WiFi.BSSIDstr();
  networks.clear();
  networks.reserve(count);
  for (int i = 0; i < count; i++)
  {
    Network network;
    network.ssid = WiFi.SSID(i);
    network.hidden = network.ssid.isEmpty();
    network.bssid = WiFi.BSSIDstr(i);
    network.rssi = WiFi.RSSI(i);
    network.channel = WiFi.channel(i);
    network.encryption = encryptionName(WiFi.encryptionType(i));
    network.connected = staConnected && network.bssid == connectedBssid;
    // A hidden network's name is known only when the device is connected to it
    if (network.hidden && network.connected)
      network.ssid = connectedSsid;
    networks.push_back(network);
  }
  WiFi.scanDelete();

  std::sort(networks.begin(), networks.end(), [](const Network &a, const Network &b)
            { return a.rssi > b.rssi; });
  return ScanState::Ready;
}
