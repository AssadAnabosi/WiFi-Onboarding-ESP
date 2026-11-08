#pragma once

#include <Arduino.h>

/**
 * @brief The settings the Settings page edits: access point and hostname.
 */
struct DeviceSettings
{
  String apSsid;
  String apPassword;
  int apChannel = 0;
  bool apHidden = false;
  bool apEnabled = true; ///< Keep the access point on even when connected to a network
  String hostname;
};

/**
 * @brief Persistent storage in NVS, which OTA updates never write to.
 *
 * Two namespaces: "cfg" holds user settings (cleared by factory reset), "sys" holds
 * system records about the firmware slots (kept). Every read falls back to a default,
 * so a new setting needs no migration. Setting names are listed in ConfigStorage.cpp.
 */
class ConfigStorage
{
public:
  // ----- User settings ("cfg" namespace) -----

  /**
   * @brief Reads the device settings; each missing one is the default from ProjectConfig.h.
   */
  static DeviceSettings loadSettings();

  /**
   * @brief Saves the device settings.
   */
  static void saveSettings(const DeviceSettings &settings);

  /**
   * @brief Prints the saved network and device settings (without passwords) to the serial log.
   */
  static void logSettings();

  /**
   * @brief The saved Wi-Fi network ("" when none).
   */
  static String getWiFiSSID();
  static String getWiFiPassword();

  /**
   * @brief Saves the Wi-Fi network to connect to on boot ("" for none).
   */
  static void saveWiFiCredentials(const String &ssid, const String &password);

  /**
   * @brief Clears user settings and the Wi-Fi driver's credentials.
   * The firmware and system records are kept.
   */
  static void factoryReset();

  // ----- System records ("sys" namespace) -----

  /**
   * @brief What one firmware slot holds.
   */
  struct SlotRecord
  {
    bool valid = false;
    String version;
    String target;
    uint8_t sha256[32] = {0};
  };

  /**
   * @brief The configuration the device was deliberately set up with (set by USB flash).
   */
  static String getHomeTarget();
  static void setHomeTarget(const String &target);

  /**
   * @brief Record for app slot 0 or 1.
   */
  static SlotRecord getSlot(int slot);
  static void setSlot(int slot, const SlotRecord &record);
  static void invalidateSlot(int slot);

  /**
   * @brief Why the last slot switch happened (see FirmwareSlots::Switch).
   */
  static String getLastSwitch();
  static void setLastSwitch(const String &reason);

  /**
   * @brief Slot an install switched to that has not confirmed itself yet (-1 for none), and why.
   */
  static int getPendingSlot(String &reason);
  static void setPendingSlot(int slot, const String &reason);
};
