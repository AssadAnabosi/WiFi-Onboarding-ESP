#include "FirmwareSlots.h"

#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>

#include "../../ProjectConfig.h"
#include "../../Version.h"
#include "../storage/ConfigStorage.h"
#include "../system/Log.h"

namespace
{
// Unconfirmed firmware is healthy once setup has finished (including Wi-Fi auto-connect)
// and it has then run this long; it is rolled back if not healthy by the deadline (from boot)
constexpr uint32_t HEALTH_UPTIME_MS = 60000;
constexpr uint32_t HEALTH_DEADLINE_MS = 180000;

int slotOf(const esp_partition_t *partition)
{
  return partition && partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1 ? 1 : 0;
}
} // namespace

// The core confirms every new firmware before setup() by default; keep it
// pending so the health check decides (the bootloader's rollback stays armed)
extern "C" bool verifyRollbackLater()
{
  return true;
}

void FirmwareSlots::begin()
{
  const esp_partition_t *running = esp_ota_get_running_partition();
  _runningSlot = slotOf(running);

  uint8_t sha[32];
  if (esp_partition_get_sha256(running, sha) != ESP_OK)
    memset(sha, 0, sizeof(sha));

  esp_ota_img_states_t state;
  _unconfirmed = esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;

  String pendingReason;
  int pendingSlot = ConfigStorage::getPendingSlot(pendingReason);
  if (pendingSlot >= 0 && pendingSlot != _runningSlot)
  {
    // The installed firmware never confirmed itself: the bootloader went back
    LOG("ota", "The last update was rolled back");
    ConfigStorage::invalidateSlot(pendingSlot);
    ConfigStorage::setLastSwitch(Switch::AUTO_ROLLBACK);
    ConfigStorage::setPendingSlot(-1, "");
    pendingSlot = -1;
  }

  ConfigStorage::SlotRecord record = ConfigStorage::getSlot(_runningSlot);
  bool known = record.valid && memcmp(record.sha256, sha, sizeof(sha)) == 0;
  if (!known && pendingSlot != _runningSlot)
  {
    // Firmware the records don't know, with no OTA install pending: flashed over USB
    LOG("ota", "New firmware from USB, resetting slot records");
    record.valid = true;
    record.version = FIRMWARE_VERSION;
    record.target = OTA_TARGET_ID;
    memcpy(record.sha256, sha, sizeof(sha));
    ConfigStorage::setSlot(_runningSlot, record);
    ConfigStorage::invalidateSlot(1 - _runningSlot);
    ConfigStorage::setHomeTarget(OTA_TARGET_ID);
    ConfigStorage::setLastSwitch(Switch::USB);
    ConfigStorage::setPendingSlot(-1, "");
  }
  else if (pendingSlot == _runningSlot && !_unconfirmed)
  {
    // Confirmed earlier, but the record was not finished
    ConfigStorage::setLastSwitch(pendingReason);
    ConfigStorage::setPendingSlot(-1, "");
  }

  if (ConfigStorage::getHomeTarget().isEmpty())
    ConfigStorage::setHomeTarget(OTA_TARGET_ID);

  LOG("ota", "Running slot %d (v%s)%s", _runningSlot, FIRMWARE_VERSION, _unconfirmed ? ", unconfirmed" : "");
  if (_unconfirmed)
    armRollbackDeadline();
}

void FirmwareSlots::armRollbackDeadline()
{
  esp_timer_create_args_t args = {};
  args.callback = rollbackDeadline;
  args.arg = this;
  args.name = "ota_deadline";
  esp_timer_handle_t timer;
  int64_t remainingUs = (int64_t)HEALTH_DEADLINE_MS * 1000 - esp_timer_get_time();
  if (esp_timer_create(&args, &timer) == ESP_OK)
    esp_timer_start_once(timer, remainingUs > 0 ? remainingUs : 1);
}

// Runs even if setup() or loop() hangs
void FirmwareSlots::rollbackDeadline(void *self)
{
  if (static_cast<FirmwareSlots *>(self)->isUnconfirmed())
  {
    LOG("ota", "Firmware not healthy in time, rolling back");
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}

void FirmwareSlots::setupDone()
{
  _setupDone = true;
  _setupDoneAt = millis();
  // While unconfirmed, a hang is caught by the task watchdog, whose reset rolls back
  if (_unconfirmed)
    enableLoopWDT();
}

void FirmwareSlots::loop()
{
  if (!_unconfirmed || !_setupDone || millis() - _setupDoneAt < HEALTH_UPTIME_MS)
    return;

  esp_ota_mark_app_valid_cancel_rollback();
  String reason;
  ConfigStorage::getPendingSlot(reason);
  ConfigStorage::setLastSwitch(reason.isEmpty() ? String(Switch::UPDATE) : reason);
  ConfigStorage::setPendingSlot(-1, "");
  _unconfirmed = false;
  disableLoopWDT();
  LOG("ota", "Firmware confirmed healthy");
}

uint32_t FirmwareSlots::confirmSecondsLeft() const
{
  if (!_unconfirmed)
    return 0;
  uint32_t now = millis();
  uint32_t healthyAt = _setupDone ? _setupDoneAt + HEALTH_UPTIME_MS : HEALTH_DEADLINE_MS;
  return healthyAt > now ? (healthyAt - now + 999) / 1000 : 0;
}

String FirmwareSlots::finishingMessage() const
{
  return "Finishing update, please wait " + String(confirmSecondsLeft()) + " s";
}

String FirmwareSlots::previousVersion() const
{
  ConfigStorage::SlotRecord other = ConfigStorage::getSlot(1 - _runningSlot);
  return other.valid ? other.version : String("");
}

bool FirmwareSlots::rollbackAvailable() const
{
  // Hidden after a manual or automatic rollback, a USB flash, and a failed or incomplete
  // update (writing an update invalidates the other slot's record). While unconfirmed the
  // last switch is not recorded yet, and rollback is refused anyway.
  return !_unconfirmed && ConfigStorage::getLastSwitch() == Switch::UPDATE &&
         ConfigStorage::getSlot(1 - _runningSlot).valid && Update.canRollBack();
}

bool FirmwareSlots::rollBack()
{
  if (!Update.rollBack())
    return false;
  ConfigStorage::setPendingSlot(1 - _runningSlot, Switch::MANUAL_ROLLBACK);
  return true;
}

void FirmwareSlots::invalidateUpdateSlot()
{
  ConfigStorage::invalidateSlot(slotOf(esp_ota_get_next_update_partition(nullptr)));
}

void FirmwareSlots::recordInstall(const String &version, const String &target)
{
  const esp_partition_t *installed = esp_ota_get_boot_partition();
  ConfigStorage::SlotRecord record;
  record.valid = esp_partition_get_sha256(installed, record.sha256) == ESP_OK;
  record.version = version;
  record.target = target;
  ConfigStorage::setSlot(slotOf(installed), record);
  ConfigStorage::setPendingSlot(slotOf(installed), Switch::UPDATE);
}
