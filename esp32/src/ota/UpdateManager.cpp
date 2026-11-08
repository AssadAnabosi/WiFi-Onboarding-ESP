#include "UpdateManager.h"

#include <Update.h>
#include <WiFi.h>
#include <algorithm>
#include <esp_flash.h>

#include "../../ProjectConfig.h"
#include "../system/BuildInfo.h"
#include "../system/Log.h"
#include "../system/Restart.h"
#include "FirmwareSlots.h"
#include "HttpGet.h"

using UpdateFile::INFO_SIZE;
using UpdateFile::SIGNATURE_SIZE;

namespace
{
constexpr size_t PARTITION_TABLE_SIZE = 0xC00;

// An upload or download that receives nothing for this long is cancelled
constexpr uint32_t UPLOAD_STALL_TIMEOUT_MS = 30000;
constexpr uint32_t DOWNLOAD_STALL_TIMEOUT_MS = 30000;

constexpr size_t DOWNLOAD_CHUNK = 4096;
constexpr uint32_t PULL_TASK_STACK = 12288;
// A pulled update reboots on its own, once the UI has had time to see the result
constexpr uint32_t PULL_REBOOT_DELAY_MS = 2000;

const char *const BUSY_MESSAGE = "An update is already running";
} // namespace

// ----- Lifecycle -----

void UpdateManager::begin()
{
  _nextCheckAt = millis() + OTA_CHECK_FIRST_DELAY_MS;

  if (!Update.installSignature(&_verifier))
    LOG("ota", "Failed to install the update signature verifier");
  LOG("ota", "%u trusted key(s)%s", (unsigned)TrustedKeyVerifier::trustedKeyCount(), FIRMWARE_DEV_BUILD ? " (dev build)" : "");

  // The flash layout an update must match: the partition table as flashed
  uint8_t *table = (uint8_t *)malloc(PARTITION_TABLE_SIZE);
  if (table && esp_flash_read(nullptr, table, CONFIG_PARTITION_TABLE_OFFSET, PARTITION_TABLE_SIZE) == ESP_OK)
  {
    mbedtls_sha256(table, PARTITION_TABLE_SIZE, _layoutHash, 0);
    _layoutHashValid = true;
  }
  else
    LOG("ota", "Failed to read the partition table");
  free(table);
}

void UpdateManager::loop()
{
  // Scheduled update checks need a network with internet; until then, look again shortly
  if ((int32_t)(millis() - _nextCheckAt) >= 0)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      _nextCheckAt = millis() + OTA_CHECK_INTERVAL_MS + esp_random() % OTA_CHECK_JITTER_MS;
      requestCheck();
    }
    else
      _nextCheckAt = millis() + OTA_CHECK_FIRST_DELAY_MS;
  }

  std::lock_guard<std::mutex> guard(_lock);
  if (_state == State::Receiving && !_pull && millis() - _lastDataAt > UPLOAD_STALL_TIMEOUT_MS)
  {
    Update.abort();
    failLocked("upload_cancelled", "Upload cancelled: no data received for 30 s");
  }
}

bool UpdateManager::isBusy() const
{
  std::lock_guard<std::mutex> guard(_lock);
  return isBusyLocked();
}

bool UpdateManager::isBusyLocked() const
{
  return _state == State::Downloading || _state == State::Receiving || _state == State::Installing || _state == State::Rebooting;
}

// While unconfirmed, the other slot is the rollback target: nothing may overwrite or boot it
bool UpdateManager::refuseLocked(UpdateReply &reply) const
{
  if (isBusyLocked())
    reply = {409, "update_running", BUSY_MESSAGE};
  else if (_slots.isUnconfirmed())
    reply = {409, "finishing_update", _slots.finishingMessage()};
  else
    return false;
  return true;
}

String UpdateManager::layoutHash() const
{
  if (!_layoutHashValid)
    return "";
  char hex[65];
  for (int i = 0; i < 32; i++)
    sprintf(hex + i * 2, "%02x", _layoutHash[i]);
  return String(hex);
}

// ----- Install path shared by uploads and downloads -----

void UpdateManager::failLocked(const char *error, const String &message)
{
  mbedtls_sha256_free(&_sha);
  _state = State::Failed;
  _error = error;
  _message = message;
  LOG("ota", "Update failed: %s", message.c_str());
}

bool UpdateManager::beginLocked(size_t total, const uint8_t *data, size_t len, const UpdateFile::Options &options)
{
  String message;
  if (const char *error = UpdateFile::checkStart(data, len, total, message))
  {
    failLocked(error, message);
    return false;
  }
  if (!Update.begin(total, U_FLASH))
  {
    bool tooLarge = Update.getError() == UPDATE_ERROR_SIZE || Update.getError() == UPDATE_ERROR_SPACE;
    if (tooLarge)
      failLocked("file_too_large", "The file is too large for this device");
    else
      failLocked("write_failed", String("Could not start the update: ") + Update.errorString());
    return false;
  }

  // The inactive slot is being overwritten: its record no longer holds
  _slots.invalidateUpdateSlot();

  _state = State::Receiving;
  _total = total;
  _received = 0;
  _options = options;
  _lastDataAt = millis();
  _error = "";
  _message = "";
  memset(_info, 0, sizeof(_info));
  mbedtls_sha256_init(&_sha);
  mbedtls_sha256_starts(&_sha, 0);
  LOG("ota", "Update started: %u bytes%s", (unsigned)total, _pull ? " (pulled)" : "");
  return true;
}

bool UpdateManager::writeLocked(const uint8_t *data, size_t len, size_t index)
{
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
  mbedtls_sha256_update(&_sha, data, len);
  _received = index + len;
  _lastDataAt = millis();
  return true;
}

bool UpdateManager::finishLocked(const uint8_t *expectedSha)
{
  if (_state != State::Receiving)
    return false;
  if (_received != _total)
  {
    Update.abort();
    failLocked("upload_cancelled", "Upload incomplete");
    return false;
  }

  uint8_t sha[32];
  mbedtls_sha256_finish(&_sha, sha);
  mbedtls_sha256_free(&_sha);
  if (expectedSha && memcmp(sha, expectedSha, sizeof(sha)) != 0)
  {
    Update.abort();
    failLocked("verification_failed", "The download doesn't match the release manifest");
    return false;
  }

  _state = State::Installing;
  UpdateFile::Info info = UpdateFile::parseInfo(_info);
  String message;
  if (const char *error = UpdateFile::checkInfo(info, _total, _layoutHashValid ? _layoutHash : nullptr, _options, message))
  {
    Update.abort();
    failLocked(error, message);
    return false;
  }

  // Verifies the signature, then switches the boot slot
  if (!Update.end())
  {
    if (Update.getError() == UPDATE_ERROR_SIGN)
      failLocked("invalid_signature", "This file isn't signed for this project.");
    else
      failLocked("verification_failed", String("Verification failed: ") + Update.errorString());
    return false;
  }

  _slots.recordInstall(info.versionString(), info.target);
  LOG("ota", "Update installed (key %s), rebooting", _verifier.matchedKeyId() ? _verifier.matchedKeyId() : "?");
  _state = State::Rebooting;
  if (_pull)
    Restart::schedule(PULL_REBOOT_DELAY_MS);
  return true;
}

// ----- Manual upload -----

UpdateReply UpdateManager::uploadBegin(size_t total, const uint8_t *data, size_t len, const UpdateFile::Options &options)
{
  std::lock_guard<std::mutex> guard(_lock);
  UpdateReply reply;
  if (refuseLocked(reply))
    return reply;
  _pull = false;
  if (!beginLocked(total, data, len, options))
    return {400, _error, _message};
  return reply;
}

bool UpdateManager::uploadWrite(const uint8_t *data, size_t len, size_t index)
{
  std::lock_guard<std::mutex> guard(_lock);
  return writeLocked(data, len, index);
}

bool UpdateManager::uploadFinish()
{
  std::lock_guard<std::mutex> guard(_lock);
  return finishLocked(nullptr);
}

void UpdateManager::uploadCancel()
{
  std::lock_guard<std::mutex> guard(_lock);
  if (_state != State::Receiving || _pull)
    return;
  Update.abort();
  failLocked("upload_cancelled", "Upload cancelled");
}

UpdateReply UpdateManager::lastFailure() const
{
  std::lock_guard<std::mutex> guard(_lock);
  return {400, _error, _message};
}

// ----- Rollback -----

UpdateReply UpdateManager::requestRollback()
{
  std::lock_guard<std::mutex> guard(_lock);
  UpdateReply reply;
  if (refuseLocked(reply))
    return reply;
  if (!_slots.rollbackAvailable())
    return {409, "", "No previous version to roll back to"};
  String version = _slots.previousVersion();
  if (!_slots.rollBack())
    return {500, "", "Rollback failed: the previous firmware is not bootable"};
  _state = State::Rebooting;
  _pull = false;
  return {200, "", "Rolling back to v" + version + ", rebooting"};
}

// ----- Update check -----

UpdateReply UpdateManager::requestCheck()
{
  {
    std::lock_guard<std::mutex> guard(_lock);
    if (_check == UpdateCheck::State::Checking)
      return {409, "", "Already checking for updates"};
    if (isBusyLocked())
      return {409, "", BUSY_MESSAGE};
    _check = UpdateCheck::State::Checking;
    _checkError = "";
    _checkMessage = "";
  }
  if (xTaskCreate(checkTask, "ota_check", PULL_TASK_STACK, this, 1, nullptr) != pdPASS)
  {
    std::lock_guard<std::mutex> guard(_lock);
    _check = UpdateCheck::State::Failed;
    _checkError = "not_enough_memory";
    _checkMessage = "Could not start the update check";
    return {500, "", _checkMessage};
  }
  return {202, "", "Checking for updates"};
}

void UpdateManager::checkTask(void *self)
{
  static_cast<UpdateManager *>(self)->runCheck();
  vTaskDelete(nullptr);
}

void UpdateManager::runCheck()
{
  UpdateCheck::Result result = UpdateCheck::run();
  std::lock_guard<std::mutex> guard(_lock);
  _check = result.state;
  _checkError = result.error;
  _checkMessage = result.message;
  if (result.state == UpdateCheck::State::Available)
    _available = result.release;
}

// ----- Pulled update -----

UpdateReply UpdateManager::requestInstall()
{
  UpdateReply reply;
  {
    std::lock_guard<std::mutex> guard(_lock);
    if (refuseLocked(reply))
      return reply;
    if (_check != UpdateCheck::State::Available)
      return {409, "", _check == UpdateCheck::State::Checking ? "Still checking for updates" : "No update available"};
    _state = State::Downloading;
    _pull = true;
    _total = _available.size;
    _received = 0;
    _error = "";
    _message = "";
    reply = {202, "", "Downloading v" + _available.version};
  }
  if (xTaskCreate(downloadTask, "ota_download", PULL_TASK_STACK, this, 1, nullptr) != pdPASS)
  {
    std::lock_guard<std::mutex> guard(_lock);
    failLocked("not_enough_memory", "Could not start the download");
    return {500, "", _message};
  }
  return reply;
}

void UpdateManager::downloadTask(void *self)
{
  static_cast<UpdateManager *>(self)->runDownload();
  vTaskDelete(nullptr);
}

void UpdateManager::runDownload()
{
  UpdateCheck::Release release;
  {
    std::lock_guard<std::mutex> guard(_lock);
    release = _available;
  }

  const char *error;
  String message;
  if (!HttpGet::canFetch(release.url, error, message))
  {
    std::lock_guard<std::mutex> guard(_lock);
    return failLocked(error, message);
  }

  uint8_t *buffer = (uint8_t *)malloc(DOWNLOAD_CHUNK);
  if (!buffer)
  {
    std::lock_guard<std::mutex> guard(_lock);
    return failLocked("not_enough_memory", "Not enough free memory for the download");
  }

  LOG("ota", "Downloading %s", release.url.c_str());
  HttpGet get;
  int code = get.open(release.url);
  int length = code == HTTP_CODE_OK ? get.http().getSize() : 0;
  if (code != HTTP_CODE_OK || (length >= 0 && (size_t)length != release.size))
  {
    free(buffer);
    std::lock_guard<std::mutex> guard(_lock);
    if (code != HTTP_CODE_OK)
      return failLocked("network_failure", code < 0 ? "Download failed: " + HTTPClient::errorToString(code)
                                                    : "The update server answered " + String(code));
    return failLocked("verification_failed", "The download doesn't match the release manifest");
  }

  // Same path as a manual upload, plus the size and SHA-256 from the manifest
  NetworkClient *stream = get.http().getStreamPtr();
  size_t received = 0;
  uint32_t lastDataAt = millis();
  bool ok = true;
  while (ok && received < release.size)
  {
    size_t available = stream->available();
    if (!available)
    {
      if (!stream->connected() || millis() - lastDataAt > DOWNLOAD_STALL_TIMEOUT_MS)
      {
        std::lock_guard<std::mutex> guard(_lock);
        if (_state == State::Receiving)
          Update.abort();
        failLocked("network_failure", "Download interrupted: the network connection was lost");
        ok = false;
      }
      else
        delay(10);
      continue;
    }
    size_t n = stream->readBytes(buffer, std::min({available, DOWNLOAD_CHUNK, release.size - received}));
    if (n == 0)
      continue;
    lastDataAt = millis();

    std::lock_guard<std::mutex> guard(_lock);
    // A pulled update never downgrades or changes the target: default options
    if (received == 0)
      ok = _state == State::Downloading && beginLocked(release.size, buffer, n, UpdateFile::Options());
    ok = ok && writeLocked(buffer, n, received);
    received += n;
  }
  free(buffer);

  if (ok)
  {
    std::lock_guard<std::mutex> guard(_lock);
    finishLocked(release.sha256);
  }
}

// ----- Status -----

const char *UpdateManager::stateName(State state, bool pull)
{
  switch (state)
  {
  case State::Downloading:
    return "downloading";
  case State::Receiving:
    return pull ? "downloading" : "receiving";
  case State::Installing:
    return "installing";
  case State::Rebooting:
    return "rebooting";
  case State::Failed:
    return "failed";
  default:
    return "idle";
  }
}

void UpdateManager::fillStatus(JsonObject out) const
{
  std::lock_guard<std::mutex> guard(_lock);
  out["state"] = stateName(_state, _pull);
  out["source"] = _pull ? "pull" : "upload";
  out["progress"] = _total ? (int)((uint64_t)_received * 100 / _total) : 0;
  out["confirming"] = _slots.isUnconfirmed();
  if (_slots.isUnconfirmed())
    out["confirm_in_s"] = _slots.confirmSecondsLeft();
  if (_state == State::Failed)
  {
    out["error"] = _error;
    out["message"] = _message;
  }

  JsonObject check = out["check"].to<JsonObject>();
  check["state"] = UpdateCheck::stateName(_check);
  if (_check == UpdateCheck::State::Available)
  {
    check["version"] = _available.version;
    check["notes"] = _available.notes;
    // Found in the local dev manifest rather than a GitHub release
    check["dev"] = UpdateCheck::usesDevManifest();
  }
  if (!_checkMessage.isEmpty())
  {
    check["error"] = _checkError;
    check["message"] = _checkMessage;
  }
}
