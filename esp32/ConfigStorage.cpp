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

// "sys" namespace (slot keys are prefixed "s0_" / "s1_" for app0 / app1)
#define KEY_HOME_TARGET "home_target"     // String
#define KEY_LAST_SWITCH "last_switch"     // String
#define KEY_PENDING_SLOT "pend_slot"      // Int
#define KEY_PENDING_REASON "pend_reason"  // String
#define KEY_SLOT_VALID "_valid"           // Bool
#define KEY_SLOT_VERSION "_version"       // String
#define KEY_SLOT_TARGET "_target"         // String
#define KEY_SLOT_SHA "_sha"               // Bytes (32)

// Reserved (retired) names: none yet

static String slotKey(int slot, const char *suffix)
{
  return String(slot == 0 ? "s0" : "s1") + suffix;
}

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

String ConfigStorage::getHomeTarget()
{
  Preferences prefs;
  if (!prefs.begin(NS_SYSTEM, true))
    return "";
  String value = prefs.isKey(KEY_HOME_TARGET) ? prefs.getString(KEY_HOME_TARGET, "") : String("");
  prefs.end();
  return value;
}

void ConfigStorage::setHomeTarget(const String &target)
{
  Preferences prefs;
  if (!prefs.begin(NS_SYSTEM, false))
    return;
  prefs.putString(KEY_HOME_TARGET, target);
  prefs.end();
}

ConfigStorage::SlotRecord ConfigStorage::getSlot(int slot)
{
  SlotRecord record;
  Preferences prefs;
  if (!prefs.begin(NS_SYSTEM, true))
    return record;
  String valid = slotKey(slot, KEY_SLOT_VALID);
  if (prefs.isKey(valid.c_str()) && prefs.getBool(valid.c_str(), false))
  {
    record.valid = true;
    record.version = prefs.getString(slotKey(slot, KEY_SLOT_VERSION).c_str(), "");
    record.target = prefs.getString(slotKey(slot, KEY_SLOT_TARGET).c_str(), "");
    record.valid = prefs.getBytes(slotKey(slot, KEY_SLOT_SHA).c_str(), record.sha256, sizeof(record.sha256)) == sizeof(record.sha256);
  }
  prefs.end();
  return record;
}

void ConfigStorage::setSlot(int slot, const SlotRecord &record)
{
  Preferences prefs;
  if (!prefs.begin(NS_SYSTEM, false))
    return;
  prefs.putBool(slotKey(slot, KEY_SLOT_VALID).c_str(), record.valid);
  prefs.putString(slotKey(slot, KEY_SLOT_VERSION).c_str(), record.version);
  prefs.putString(slotKey(slot, KEY_SLOT_TARGET).c_str(), record.target);
  prefs.putBytes(slotKey(slot, KEY_SLOT_SHA).c_str(), record.sha256, sizeof(record.sha256));
  prefs.end();
}

void ConfigStorage::invalidateSlot(int slot)
{
  Preferences prefs;
  if (!prefs.begin(NS_SYSTEM, false))
    return;
  prefs.putBool(slotKey(slot, KEY_SLOT_VALID).c_str(), false);
  prefs.end();
}

String ConfigStorage::getLastSwitch()
{
  Preferences prefs;
  if (!prefs.begin(NS_SYSTEM, true))
    return "";
  String value = prefs.isKey(KEY_LAST_SWITCH) ? prefs.getString(KEY_LAST_SWITCH, "") : String("");
  prefs.end();
  return value;
}

void ConfigStorage::setLastSwitch(const String &reason)
{
  Preferences prefs;
  if (!prefs.begin(NS_SYSTEM, false))
    return;
  prefs.putString(KEY_LAST_SWITCH, reason);
  prefs.end();
}

int ConfigStorage::getPendingSlot(String &reason)
{
  Preferences prefs;
  if (!prefs.begin(NS_SYSTEM, true))
    return -1;
  int slot = prefs.isKey(KEY_PENDING_SLOT) ? prefs.getInt(KEY_PENDING_SLOT, -1) : -1;
  reason = prefs.isKey(KEY_PENDING_REASON) ? prefs.getString(KEY_PENDING_REASON, "") : String("");
  prefs.end();
  return slot;
}

void ConfigStorage::setPendingSlot(int slot, const String &reason)
{
  Preferences prefs;
  if (!prefs.begin(NS_SYSTEM, false))
    return;
  prefs.putInt(KEY_PENDING_SLOT, slot);
  prefs.putString(KEY_PENDING_REASON, reason);
  prefs.end();
}
