#include "ConfigStorage.h"
#include "ProjectConfig.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

#define FORMAT_LITTLEFS_IF_FAILED true
#define CONFIG_FILE "/config.json"

void ConfigStorage::begin()
{
  if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED))
  {
    Serial.println("LittleFS Mount Failed");
    return;
  }

  if (!LittleFS.exists(CONFIG_FILE))
  {
    File f = LittleFS.open(CONFIG_FILE, "w");
    if (f)
    {
      StaticJsonDocument<512> doc;
      doc["wifi_ssid"] = "";
      doc["wifi_password"] = "";
      doc["ap_ssid"] = DEFAULT_AP_SSID;
      doc["ap_password"] = DEFAULT_AP_PASSWORD;
      doc["ap_channel"] = DEFAULT_AP_CHANNEL;
      doc["ap_hidden"] = DEFAULT_AP_HIDDEN;
      doc["ap_status"] = DEFAULT_AP_ENABLED;
      doc["hostname"] = DEFAULT_HOSTNAME;
      serializeJson(doc, f);
      f.close();
      Serial.println("Config file created with default values.");
    }
    else
    {
      Serial.println("Failed to create config file.");
    }
  }
  else
  {
    Serial.println("Config file already exists.");
  }
  Serial.println("----- Current Settings -----");
  Serial.print("WiFi SSID: ");
  Serial.println(ConfigStorage::getWiFiSSID());
  Serial.print("WiFi Password: ");
  Serial.println(ConfigStorage::getWiFiPassword());
  Serial.print("Hostname: ");
  Serial.println(ConfigStorage::getHostname());

  Serial.print("AP SSID: ");
  Serial.println(ConfigStorage::getAPSSID());
  Serial.print("AP Password: ");
  Serial.println(ConfigStorage::getAPPassword());
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
  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f)
    return;
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, f);
  f.close();
  if (error)
    return;

  doc["wifi_ssid"] = ssid;
  doc["wifi_password"] = password;

  f = LittleFS.open(CONFIG_FILE, "w");
  if (f)
  {
    serializeJson(doc, f);
    f.close();
  }
}

String ConfigStorage::getWiFiSSID()
{
  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f)
    return "";
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, f);
  f.close();
  if (error)
    return "";
  return doc["wifi_ssid"] | "";
}

String ConfigStorage::getWiFiPassword()
{
  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f)
    return "";
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, f);
  f.close();
  if (error)
    return "";
  return doc["wifi_password"] | "";
}

void ConfigStorage::saveSettings(const String &ssid, const String &password, int channel, bool hidden, bool status, const String &hostname)
{
  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f)
    return;
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, f);
  f.close();
  if (error)
    return;

  doc["ap_ssid"] = ssid;
  doc["ap_password"] = password;
  doc["ap_channel"] = channel;
  doc["ap_hidden"] = hidden;
  doc["ap_status"] = status;
  doc["hostname"] = hostname;

  f = LittleFS.open(CONFIG_FILE, "w");
  if (f)
  {
    serializeJson(doc, f);
    f.close();
  }
}

String ConfigStorage::getHostname()
{
  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f)
    return DEFAULT_HOSTNAME;
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, f);
  f.close();
  if (error)
    return DEFAULT_HOSTNAME;
  return doc["hostname"] | DEFAULT_HOSTNAME;
}

String ConfigStorage::getAPSSID()
{
  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f)
    return "ap_ssid_error_open_file";
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, f);
  f.close();
  if (error)
    return "ap_ssid_error_deserialize";

  return doc["ap_ssid"] | "ap_ssid_error_doc_get";
}

String ConfigStorage::getAPPassword()
{
  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f)
    return "ap_password_error";
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, f);
  f.close();
  if (error)
    return "ap_password_error";
  return doc["ap_password"] | "ap_password_error";
}

int ConfigStorage::getAPChannel()
{
  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f)
    return DEFAULT_AP_CHANNEL;
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, f);
  f.close();
  if (error)
    return DEFAULT_AP_CHANNEL;
  return doc["ap_channel"] | DEFAULT_AP_CHANNEL;
}

bool ConfigStorage::getAPHidden()
{
  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f)
    return DEFAULT_AP_HIDDEN;
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, f);
  f.close();
  if (error)
    return DEFAULT_AP_HIDDEN;
  return doc["ap_hidden"] | DEFAULT_AP_HIDDEN;
}

bool ConfigStorage::getAPStatus()
{
  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f)
    return DEFAULT_AP_ENABLED;
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, f);
  f.close();
  if (error)
    return DEFAULT_AP_ENABLED;
  return doc["ap_status"] | DEFAULT_AP_ENABLED;
}

void ConfigStorage::factoryReset()
{
  if (LittleFS.exists(CONFIG_FILE))
  {
    LittleFS.remove(CONFIG_FILE);
  }
  begin(); // recreate file with defaults
}