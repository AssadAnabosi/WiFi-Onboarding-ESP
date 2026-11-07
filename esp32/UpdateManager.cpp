#include "UpdateManager.h"
#include "ProjectConfig.h"
#include "BuildInfo.h"
#include <Update.h>
#include <esp_flash.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

UpdateManager updateManager;

// Update file layout
#define INFO_SIZE 128
#define SIGNATURE_SIZE 512
#define INFO_MAGIC "WOTA"
#define INFO_FORMAT_VERSION 1

// Info block field offsets (little-endian, text fields zero-padded)
#define INFO_OFF_FORMAT 4
#define INFO_OFF_FIRMWARE_LENGTH 6
#define INFO_OFF_VERSION 10
#define INFO_OFF_CHIP 16
#define INFO_OFF_LAYOUT 18
#define INFO_OFF_TARGET 50
#define INFO_OFF_SLUG 82
#define INFO_TEXT_SIZE 32

#define ESP_IMAGE_MAGIC 0xE9
#define PARTITION_TABLE_SIZE 0xC00

// An upload that sends nothing for this long is cancelled
#define UPLOAD_STALL_TIMEOUT_MS 30000

static uint16_t readU16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t readU32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static String readText(const uint8_t *p)
{
  char text[INFO_TEXT_SIZE + 1];
  memcpy(text, p, INFO_TEXT_SIZE);
  text[INFO_TEXT_SIZE] = '\0';
  return String(text);
}

static int compareVersions(uint16_t major, uint16_t minor, uint16_t patch)
{
  uint64_t a = ((uint64_t)major << 32) | ((uint64_t)minor << 16) | patch;
  uint64_t b = ((uint64_t)FIRMWARE_VERSION_MAJOR << 32) | ((uint64_t)FIRMWARE_VERSION_MINOR << 16) | FIRMWARE_VERSION_PATCH;
  return (a > b) - (a < b);
}

static const char *stateName(UpdateManager::State state)
{
  switch (state)
  {
  case UpdateManager::State::Receiving:
    return "receiving";
  case UpdateManager::State::Installing:
    return "installing";
  case UpdateManager::State::Rebooting:
    return "rebooting";
  case UpdateManager::State::Failed:
    return "failed";
  default:
    return "idle";
  }
}

void UpdateManager::begin()
{
  if (!Update.installSignature(&_verifier))
    Serial.println("Failed to install the update signature verifier");
  Serial.printf("OTA: %u trusted key(s)%s\n", (unsigned)TrustedKeyVerifier::trustedKeyCount(), FIRMWARE_DEV_BUILD ? " (dev build)" : "");

  // The flash layout an update must match: the partition table as flashed
  uint8_t *table = (uint8_t *)malloc(PARTITION_TABLE_SIZE);
  if (table && esp_flash_read(NULL, table, CONFIG_PARTITION_TABLE_OFFSET, PARTITION_TABLE_SIZE) == ESP_OK)
  {
    mbedtls_sha256(table, PARTITION_TABLE_SIZE, _layoutHash, 0);
    _layoutHashValid = true;
  }
  else
    Serial.println("Failed to read the partition table");
  free(table);
}

void UpdateManager::loop()
{
  std::lock_guard<std::mutex> guard(_lock);
  if (_state == State::Receiving && millis() - _lastDataAt > UPLOAD_STALL_TIMEOUT_MS)
  {
    Update.abort();
    failLocked("upload_cancelled", "Upload cancelled: no data received for 30 s");
  }
}

bool UpdateManager::isBusy() const
{
  std::lock_guard<std::mutex> guard(_lock);
  return _state == State::Receiving || _state == State::Installing || _state == State::Rebooting;
}

void UpdateManager::failLocked(const char *reason, const String &message)
{
  _state = State::Failed;
  _reason = reason;
  _message = message;
  Serial.println("Update failed: " + message);
}

bool UpdateManager::uploadBegin(size_t total, const uint8_t *data, size_t len, const UploadOptions &options, String &reason, String &message)
{
  std::lock_guard<std::mutex> guard(_lock);
  if (_state == State::Receiving || _state == State::Installing || _state == State::Rebooting)
  {
    reason = "update_running";
    message = "An update is already running";
    return false;
  }

  // Classic ESP32 factory images start with padding before the bootloader
  if (len > 0 && data[0] == 0xFF)
  {
    reason = "factory_image";
    message = "This is a factory image for USB flashing, not an update";
    failLocked(reason.c_str(), message);
    return false;
  }
  if (len == 0 || data[0] != ESP_IMAGE_MAGIC || total <= INFO_SIZE + SIGNATURE_SIZE)
  {
    reason = "invalid_file";
    message = "This file is not a firmware update";
    failLocked(reason.c_str(), message);
    return false;
  }
  if (!Update.begin(total, U_FLASH))
  {
    bool tooLarge = Update.getError() == UPDATE_ERROR_SIZE || Update.getError() == UPDATE_ERROR_SPACE;
    reason = tooLarge ? "file_too_large" : "write_failed";
    message = tooLarge ? "The file is too large for this device" : String("Could not start the update: ") + Update.errorString();
    failLocked(reason.c_str(), message);
    return false;
  }

  _state = State::Receiving;
  _total = total;
  _received = 0;
  _options = options;
  _lastDataAt = millis();
  _reason = "";
  _message = "";
  memset(_info, 0, sizeof(_info));
  Serial.printf("Update started: %u bytes\n", (unsigned)total);
  return true;
}

bool UpdateManager::uploadWrite(const uint8_t *data, size_t len, size_t index)
{
  std::lock_guard<std::mutex> guard(_lock);
  if (_state != State::Receiving)
    return false;

  // Keep the info block in RAM as it streams past
  size_t infoStart = _total - SIGNATURE_SIZE - INFO_SIZE;
  for (size_t i = 0; i < len; i++)
  {
    size_t pos = index + i;
    if (pos >= infoStart && pos < infoStart + INFO_SIZE)
      _info[pos - infoStart] = data[i];
  }

  if (Update.write((uint8_t *)data, len) != len)
  {
    String error = Update.errorString();
    Update.abort();
    failLocked("write_failed", "Writing the update failed: " + error);
    return false;
  }
  _received = index + len;
  _lastDataAt = millis();
  return true;
}

bool UpdateManager::checkInfoLocked()
{
  if (memcmp(_info, INFO_MAGIC, 4) != 0 || readU16(_info + INFO_OFF_FORMAT) != INFO_FORMAT_VERSION)
  {
    failLocked("invalid_file", "This file is not an update for this project");
    return false;
  }
  if (readU32(_info + INFO_OFF_FIRMWARE_LENGTH) != _total - INFO_SIZE - SIGNATURE_SIZE)
  {
    failLocked("invalid_file", "The update file is damaged");
    return false;
  }
  if (readText(_info + INFO_OFF_SLUG) != PROJECT_SLUG)
  {
    failLocked("wrong_project", "This file is for another project");
    return false;
  }
  if (readU16(_info + INFO_OFF_CHIP) != CONFIG_IDF_FIRMWARE_CHIP_ID)
  {
    failLocked("wrong_target", "This file is for another chip and cannot run on this device");
    return false;
  }
  if (!_layoutHashValid || memcmp(_info + INFO_OFF_LAYOUT, _layoutHash, 32) != 0)
  {
    failLocked("wrong_target", "This file is for another flash layout and cannot run on this device");
    return false;
  }
  String target = readText(_info + INFO_OFF_TARGET);
  if (target != OTA_TARGET_ID && !_options.allowTargetChange)
  {
    failLocked("wrong_target", "This file is for " + target + ". This device runs " OTA_TARGET_ID);
    return false;
  }
  const uint8_t *v = _info + INFO_OFF_VERSION;
  if (compareVersions(readU16(v), readU16(v + 2), readU16(v + 4)) <= 0 && !_options.allowDowngrade)
  {
    failLocked("not_newer", "This file is not newer than the installed version");
    return false;
  }
  return true;
}

bool UpdateManager::uploadFinish()
{
  std::lock_guard<std::mutex> guard(_lock);
  if (_state != State::Receiving)
    return false;
  if (_received != _total)
  {
    Update.abort();
    failLocked("upload_cancelled", "Upload incomplete");
    return false;
  }

  _state = State::Installing;
  if (!checkInfoLocked())
  {
    Update.abort();
    return false;
  }

  // Verifies the signature, then switches the boot slot
  if (!Update.end())
  {
    bool badSignature = Update.getError() == UPDATE_ERROR_SIGN;
    failLocked(badSignature ? "invalid_signature" : "verification_failed",
               badSignature ? "This file isn't signed for this project" : String("Verification failed: ") + Update.errorString());
    return false;
  }

  Serial.printf("Update installed (key %s), rebooting\n", _verifier.matchedKeyId() ? _verifier.matchedKeyId() : "?");
  _state = State::Rebooting;
  return true;
}

void UpdateManager::uploadCancel()
{
  std::lock_guard<std::mutex> guard(_lock);
  if (_state != State::Receiving)
    return;
  Update.abort();
  failLocked("upload_cancelled", "Upload cancelled");
}

void UpdateManager::fillStatus(JsonObject out)
{
  std::lock_guard<std::mutex> guard(_lock);
  out["state"] = stateName(_state);
  out["progress"] = _total ? (int)((uint64_t)_received * 100 / _total) : 0;
  if (_state == State::Failed)
  {
    out["error"] = _reason;
    out["message"] = _message;
  }
}

String UpdateManager::lastReason()
{
  std::lock_guard<std::mutex> guard(_lock);
  return _reason;
}

String UpdateManager::lastMessage()
{
  std::lock_guard<std::mutex> guard(_lock);
  return _message;
}
