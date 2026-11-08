#include "ConfigStorage.h"

#include <Preferences.h>
#include <esp_wifi.h>

#include "../../ProjectConfig.h"
#include "../system/Log.h"

/**
 * Setting names (see CONTRIBUTING.md): never rename, reuse or change the type of a
 * name; every read takes a default; retired names stay listed as reserved.
 * Names are at most 15 characters.
 */
namespace
{
// Namespaces
constexpr const char *NS_CONFIG = "cfg"; // User settings, cleared by factory reset
constexpr const char *NS_SYSTEM = "sys"; // System records, kept by factory reset

// "cfg" namespace
constexpr const char *KEY_WIFI_SSID = "wifi_ssid";         // String
constexpr const char *KEY_WIFI_PASSWORD = "wifi_password"; // String
constexpr const char *KEY_AP_SSID = "ap_ssid";             // String
constexpr const char *KEY_AP_PASSWORD = "ap_password";     // String
constexpr const char *KEY_AP_CHANNEL = "ap_channel";       // Int
constexpr const char *KEY_AP_HIDDEN = "ap_hidden";         // Bool
constexpr const char *KEY_AP_STATUS = "ap_status";         // Bool
constexpr const char *KEY_HOSTNAME = "hostname";           // String

// "sys" namespace (slot keys are prefixed "s0" / "s1" for app0 / app1)
constexpr const char *KEY_HOME_TARGET = "home_target";    // String
constexpr const char *KEY_LAST_SWITCH = "last_switch";    // String
constexpr const char *KEY_PENDING_SLOT = "pend_slot";     // Int
constexpr const char *KEY_PENDING_REASON = "pend_reason"; // String
constexpr const char *KEY_SLOT_VALID = "_valid";          // Bool
constexpr const char *KEY_SLOT_VERSION = "_version";      // String
constexpr const char *KEY_SLOT_TARGET = "_target";        // String
constexpr const char *KEY_SLOT_SHA = "_sha";              // Bytes (32)

// Reserved (retired) names: none yet

String slotKey(int slot, const char *suffix)
{
  return String(slot == 0 ? "s0" : "s1") + suffix;
}

// Reads take a default for a missing key, and also when the namespace could not be
// opened (it does not exist until something is written to it)
String getString(Preferences &prefs, const char *key, const String &defaultValue)
{
  return prefs.isKey(key) ? prefs.getString(key, defaultValue) : defaultValue;
}

int getInt(Preferences &prefs, const char *key, int defaultValue)
{
  return prefs.isKey(key) ? prefs.getInt(key, defaultValue) : defaultValue;
}

bool getBool(Preferences &prefs, const char *key, bool defaultValue)
{
  return prefs.isKey(key) ? prefs.getBool(key, defaultValue) : defaultValue;
}

String readString(const char *ns, const char *key, const String &defaultValue)
{
  Preferences prefs;
  prefs.begin(ns, true);
  String value = getString(prefs, key, defaultValue);
  prefs.end();
  return value;
}

bool openForWrite(Preferences &prefs, const char *ns)
{
  if (prefs.begin(ns, false))
    return true;
  LOG("cfg", "Failed to open the \"%s\" storage", ns);
  return false;
}

void writeString(const char *ns, const char *key, const String &value)
{
  Preferences prefs;
  if (!openForWrite(prefs, ns))
    return;
  prefs.putString(key, value);
  prefs.end();
}
} // namespace

// ----- User settings -----

DeviceSettings ConfigStorage::loadSettings()
{
  Preferences prefs;
  prefs.begin(NS_CONFIG, true);
  DeviceSettings settings;
  settings.apSsid = getString(prefs, KEY_AP_SSID, DEFAULT_AP_SSID);
  settings.apPassword = getString(prefs, KEY_AP_PASSWORD, DEFAULT_AP_PASSWORD);
  settings.apChannel = getInt(prefs, KEY_AP_CHANNEL, DEFAULT_AP_CHANNEL);
  settings.apHidden = getBool(prefs, KEY_AP_HIDDEN, DEFAULT_AP_HIDDEN);
  settings.apEnabled = getBool(prefs, KEY_AP_STATUS, DEFAULT_AP_ENABLED);
  settings.hostname = getString(prefs, KEY_HOSTNAME, DEFAULT_HOSTNAME);
  prefs.end();
  return settings;
}

void ConfigStorage::saveSettings(const DeviceSettings &settings)
{
  Preferences prefs;
  if (!openForWrite(prefs, NS_CONFIG))
    return;
  prefs.putString(KEY_AP_SSID, settings.apSsid);
  prefs.putString(KEY_AP_PASSWORD, settings.apPassword);
  prefs.putInt(KEY_AP_CHANNEL, settings.apChannel);
  prefs.putBool(KEY_AP_HIDDEN, settings.apHidden);
  prefs.putBool(KEY_AP_STATUS, settings.apEnabled);
  prefs.putString(KEY_HOSTNAME, settings.hostname);
  prefs.end();
}

void ConfigStorage::logSettings()
{
  DeviceSettings settings = loadSettings();
  LOG("cfg", "Wi-Fi network: %s", getWiFiSSID().c_str());
  LOG("cfg", "Hostname: %s", settings.hostname.c_str());
  LOG("cfg", "AP SSID: %s, channel %d, %s, %s", settings.apSsid.c_str(), settings.apChannel,
      settings.apHidden ? "hidden" : "visible", settings.apEnabled ? "enabled" : "disabled");
}

String ConfigStorage::getWiFiSSID()
{
  return readString(NS_CONFIG, KEY_WIFI_SSID, "");
}

String ConfigStorage::getWiFiPassword()
{
  return readString(NS_CONFIG, KEY_WIFI_PASSWORD, "");
}

void ConfigStorage::saveWiFiCredentials(const String &ssid, const String &password)
{
  Preferences prefs;
  if (!openForWrite(prefs, NS_CONFIG))
    return;
  prefs.putString(KEY_WIFI_SSID, ssid);
  prefs.putString(KEY_WIFI_PASSWORD, password);
  prefs.end();
}

void ConfigStorage::factoryReset()
{
  // Clears user settings only: the firmware and the "sys" namespace are kept
  Preferences prefs;
  if (openForWrite(prefs, NS_CONFIG))
  {
    prefs.clear();
    prefs.end();
  }
  // Also drop any credentials the Wi-Fi driver kept on its own
  esp_wifi_restore();
}

// ----- System records -----

String ConfigStorage::getHomeTarget()
{
  return readString(NS_SYSTEM, KEY_HOME_TARGET, "");
}

void ConfigStorage::setHomeTarget(const String &target)
{
  writeString(NS_SYSTEM, KEY_HOME_TARGET, target);
}

ConfigStorage::SlotRecord ConfigStorage::getSlot(int slot)
{
  SlotRecord record;
  Preferences prefs;
  prefs.begin(NS_SYSTEM, true);
  if (getBool(prefs, slotKey(slot, KEY_SLOT_VALID).c_str(), false))
  {
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
  if (!openForWrite(prefs, NS_SYSTEM))
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
  if (!openForWrite(prefs, NS_SYSTEM))
    return;
  prefs.putBool(slotKey(slot, KEY_SLOT_VALID).c_str(), false);
  prefs.end();
}

String ConfigStorage::getLastSwitch()
{
  return readString(NS_SYSTEM, KEY_LAST_SWITCH, "");
}

void ConfigStorage::setLastSwitch(const String &reason)
{
  writeString(NS_SYSTEM, KEY_LAST_SWITCH, reason);
}

int ConfigStorage::getPendingSlot(String &reason)
{
  Preferences prefs;
  prefs.begin(NS_SYSTEM, true);
  int slot = getInt(prefs, KEY_PENDING_SLOT, -1);
  reason = getString(prefs, KEY_PENDING_REASON, "");
  prefs.end();
  return slot;
}

void ConfigStorage::setPendingSlot(int slot, const String &reason)
{
  Preferences prefs;
  if (!openForWrite(prefs, NS_SYSTEM))
    return;
  prefs.putInt(KEY_PENDING_SLOT, slot);
  prefs.putString(KEY_PENDING_REASON, reason);
  prefs.end();
}
