#include "ConfigStorage.h"
#include "ProjectConfig.h"
#include <Preferences.h>
#include <esp_wifi.h>

/**
 * Settings live in NVS, which OTA updates never write to.
 *
 * Setting names (see CONTRIBUTING.md): never rename, reuse or change the type of a
 * name; every read takes a default; retired names stay listed as reserved.
 * Names are at most 15 characters.
 */

// Namespaces
#define NS_CONFIG "cfg" // User settings, cleared by factory reset
#define NS_SYSTEM "sys" // System records, kept by factory reset

// "cfg" namespace
#define KEY_WIFI_SSID "wifi_ssid"         // String
#define KEY_WIFI_PASSWORD "wifi_password" // String
#define KEY_AP_SSID "ap_ssid"             // String
#define KEY_AP_PASSWORD "ap_password"     // String
#define KEY_AP_CHANNEL "ap_channel"       // Int
#define KEY_AP_HIDDEN "ap_hidden"         // Bool
#define KEY_AP_STATUS "ap_status"         // Bool
#define KEY_HOSTNAME "hostname"           // String

// Reserved (retired) names: none yet

static String readString(const char *key, const char *defaultValue)
{
  Preferences prefs;
  if (!prefs.begin(NS_CONFIG, true))
    return defaultValue;
  String value = prefs.isKey(key) ? prefs.getString(key, defaultValue) : String(defaultValue);
  prefs.end();
  return value;
}

static int readInt(const char *key, int defaultValue)
{
  Preferences prefs;
  if (!prefs.begin(NS_CONFIG, true))
    return defaultValue;
  int value = prefs.isKey(key) ? prefs.getInt(key, defaultValue) : defaultValue;
  prefs.end();
  return value;
}

static bool readBool(const char *key, bool defaultValue)
{
  Preferences prefs;
  if (!prefs.begin(NS_CONFIG, true))
    return defaultValue;
  bool value = prefs.isKey(key) ? prefs.getBool(key, defaultValue) : defaultValue;
  prefs.end();
  return value;
}

void ConfigStorage::begin()
{
  Serial.println("----- Current Settings -----");
  Serial.print("WiFi SSID: ");
  Serial.println(ConfigStorage::getWiFiSSID());
  Serial.print("Hostname: ");
  Serial.println(ConfigStorage::getHostname());

  Serial.print("AP SSID: ");
  Serial.println(ConfigStorage::getAPSSID());
  Serial.print("AP Channel: ");
  Serial.println(ConfigStorage::getAPChannel());
  Serial.print("AP Hidden: ");
  Serial.println(ConfigStorage::getAPHidden() ? "Yes" : "No");
  Serial.print("AP Status: ");
  Serial.println(ConfigStorage::getAPStatus() ? "Enabled" : "Disabled");
  Serial.println("-----------------------------");
}

void ConfigStorage::saveWiFiCredentials(const String &ssid, const String &password)
{
  Preferences prefs;
  if (!prefs.begin(NS_CONFIG, false))
  {
    Serial.println("Failed to open settings storage");
    return;
  }
  prefs.putString(KEY_WIFI_SSID, ssid);
  prefs.putString(KEY_WIFI_PASSWORD, password);
  prefs.end();
}

String ConfigStorage::getWiFiSSID()
{
  return readString(KEY_WIFI_SSID, "");
}

String ConfigStorage::getWiFiPassword()
{
  return readString(KEY_WIFI_PASSWORD, "");
}

void ConfigStorage::saveSettings(const String &ssid, const String &password, int channel, bool hidden, bool status, const String &hostname)
{
  Preferences prefs;
  if (!prefs.begin(NS_CONFIG, false))
  {
    Serial.println("Failed to open settings storage");
    return;
  }
  prefs.putString(KEY_AP_SSID, ssid);
  prefs.putString(KEY_AP_PASSWORD, password);
  prefs.putInt(KEY_AP_CHANNEL, channel);
  prefs.putBool(KEY_AP_HIDDEN, hidden);
  prefs.putBool(KEY_AP_STATUS, status);
  prefs.putString(KEY_HOSTNAME, hostname);
  prefs.end();
}

String ConfigStorage::getHostname()
{
  return readString(KEY_HOSTNAME, DEFAULT_HOSTNAME);
}

String ConfigStorage::getAPSSID()
{
  return readString(KEY_AP_SSID, DEFAULT_AP_SSID);
}

String ConfigStorage::getAPPassword()
{
  return readString(KEY_AP_PASSWORD, DEFAULT_AP_PASSWORD);
}

int ConfigStorage::getAPChannel()
{
  return readInt(KEY_AP_CHANNEL, DEFAULT_AP_CHANNEL);
}

bool ConfigStorage::getAPHidden()
{
  return readBool(KEY_AP_HIDDEN, DEFAULT_AP_HIDDEN);
}

bool ConfigStorage::getAPStatus()
{
  return readBool(KEY_AP_STATUS, DEFAULT_AP_ENABLED);
}

void ConfigStorage::factoryReset()
{
  // Clears user settings only: the firmware and the "sys" namespace are kept
  Preferences prefs;
  if (prefs.begin(NS_CONFIG, false))
  {
    prefs.clear();
    prefs.end();
  }
  // Also drop any credentials the Wi-Fi driver kept on its own
  esp_wifi_restore();
}
