#pragma once

#include <Arduino.h>
#include <atomic>

/**
 * @brief The two app slots: what each holds, why the device last switched, and the
 * confirmation window of newly installed firmware.
 *
 * New firmware boots pending (verifyRollbackLater() returns true). It confirms itself once
 * setup() has finished and it has run 60 s more. Until then the loop watchdog is on, and an
 * esp_timer rolls it back at 3 min from boot; a reset before that also rolls back.
 *
 * Slot records live in ConfigStorage ("sys"). On every boot the running firmware's SHA-256
 * is compared with them to detect a USB flash or a rollback by the bootloader.
 */
class FirmwareSlots
{
public:
  /**
   * @brief Why the last slot switch happened. Stored in NVS: never change these values.
   */
  struct Switch
  {
    static constexpr const char *UPDATE = "update";
    static constexpr const char *MANUAL_ROLLBACK = "manual_rollback";
    static constexpr const char *AUTO_ROLLBACK = "auto_rollback";
    static constexpr const char *USB = "usb";
  };

  /**
   * @brief Reads the slot records and arms the rollback deadline of unconfirmed firmware.
   * Call first in setup().
   */
  void begin();

  /**
   * @brief Call at the end of setup(): starts the health check of unconfirmed firmware.
   */
  void setupDone();

  /**
   * @brief Confirms the running firmware once it is healthy. Call from loop().
   */
  void loop();

  /**
   * @brief True while newly installed firmware has not confirmed itself yet.
   * Reboots, settings changes, factory reset and new updates are refused meanwhile.
   */
  bool isUnconfirmed() const { return _unconfirmed; }

  /**
   * @brief Seconds until the firmware should confirm itself (0 when confirmed).
   */
  uint32_t confirmSecondsLeft() const;

  /**
   * @brief The answer to requests refused while unconfirmed: "Finishing update, please wait N s".
   */
  String finishingMessage() const;

  /**
   * @brief Version in the other slot, or "" when it holds no valid firmware.
   */
  String previousVersion() const;

  /**
   * @brief True when the Roll back button applies: the firmware is confirmed, the last switch
   * was a successful update and the other slot still holds the previous firmware.
   */
  bool rollbackAvailable() const;

  /**
   * @brief Sets the previous firmware to boot next, recorded as a pending manual rollback.
   * The caller reboots. The previous firmware boots pending and goes through the same health check.
   * @return False if the previous firmware is not bootable.
   */
  bool rollBack();

  /**
   * @brief Marks the slot an update is about to overwrite as holding no valid firmware.
   */
  void invalidateUpdateSlot();

  /**
   * @brief Records the firmware just installed into the boot slot; it stays pending until it confirms itself.
   */
  void recordInstall(const String &version, const String &target);

private:
  std::atomic<bool> _unconfirmed{false};
  bool _setupDone = false;
  uint32_t _setupDoneAt = 0;
  int _runningSlot = 0;

  void armRollbackDeadline();
  static void rollbackDeadline(void *self);
};
