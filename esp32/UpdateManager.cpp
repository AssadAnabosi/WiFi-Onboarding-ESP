#include "UpdateManager.h"
#include "ProjectConfig.h"
#include "BuildInfo.h"
#include "ConfigStorage.h"
#include <Update.h>
#include <esp_flash.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <esp_timer.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>

#if FIRMWARE_DEV_BUILD
#include "OtaPublicKey.local.h" // may set OTA_DEV_MANIFEST_URL
#endif

// Dev builds may check a local manifest (utils/keygen.py --dev-manifest-url), plain HTTP allowed
#if FIRMWARE_DEV_BUILD && defined(OTA_DEV_MANIFEST_URL)
#define OTA_MANIFEST_URL OTA_DEV_MANIFEST_URL
#else
#define OTA_MANIFEST_URL "https://github.com/" OTA_GITHUB_OWNER "/" OTA_GITHUB_REPO "/releases/latest/download/manifest.json"
#endif

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

// Health check of a newly installed firmware: healthy once setup has finished
// (including Wi-Fi auto-connect) and it has then run this long; rolled back if
// not healthy by the deadline (measured from boot)
#define HEALTH_UPTIME_MS 60000UL
#define HEALTH_DEADLINE_MS 180000UL

// Pulled updates
#define MANIFEST_SCHEMA 1
#define MANIFEST_MAX_SIZE 16384
#define NOTES_MAX_SIZE 4096
#define HTTPS_MIN_FREE_HEAP 61440
#define DOWNLOAD_CHUNK 4096
#define DOWNLOAD_STALL_TIMEOUT_MS 30000
#define HTTP_TIMEOUT_MS 15000
#define PULL_TASK_STACK 12288
#define PULL_REBOOT_DELAY_MS 2000

// The core confirms every new firmware before setup() by default; keep it
// pending so the health check decides (bootloader rollback stays armed)
extern "C" bool verifyRollbackLater()
{
  return true;
}

// Runs even if setup() or loop() hangs
static void healthDeadline(void *)
{
  if (updateManager.isUnconfirmed())
  {
    Serial.println("OTA: firmware not healthy in time, rolling back");
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}

static int slotOf(const esp_partition_t *partition)
{
  return partition && partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1 ? 1 : 0;
}

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

static bool parseVersion(const char *text, uint16_t version[3])
{
  unsigned major, minor, patch;
  char extra;
  if (!text || sscanf(text, "%u.%u.%u%c", &major, &minor, &patch, &extra) != 3 || major > 0xFFFF || minor > 0xFFFF || patch > 0xFFFF)
    return false;
  version[0] = major;
  version[1] = minor;
  version[2] = patch;
  return true;
}

static bool parseHex(const char *hex, uint8_t *out, size_t size)
{
  if (!hex || strlen(hex) != size * 2)
    return false;
  for (size_t i = 0; i < size; i++)
  {
    char byte[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
    char *end;
    out[i] = strtoul(byte, &end, 16);
    if (*end)
      return false;
  }
  return true;
}

// Release builds only fetch over HTTPS
static bool allowedUrl(const String &url)
{
  return url.startsWith("https://") || (FIRMWARE_DEV_BUILD && url.startsWith("http://"));
}

// Cut at most `max` bytes without splitting a UTF-8 character
static String truncateUtf8(const String &text, size_t max)
{
  if (text.length() <= max)
    return text;
  size_t end = max;
  while (end > 0 && (text[end] & 0xC0) == 0x80)
    end--;
  return text.substring(0, end);
}

/**
 * @brief One HTTP(S) GET that follows redirects (GitHub downloads redirect to another host).
 * HTTPS uses the core's built-in root certificate bundle. No NTP: certificate dates are not checked.
 */
class HttpGet
{
public:
  int open(const String &url)
  {
    NetworkClient *client = &_plain;
    if (url.startsWith("https://"))
    {
      _secure.useBuiltinCACertBundle();
      client = &_secure;
    }
    _http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    _http.setConnectTimeout(HTTP_TIMEOUT_MS);
    _http.setTimeout(HTTP_TIMEOUT_MS);
    _http.useHTTP10(true); // no chunked encoding: the body is read as a plain stream
    if (!_http.begin(*client, url))
      return HTTPC_ERROR_CONNECTION_REFUSED;
    return _http.GET();
  }
  HTTPClient &http() { return _http; }
  ~HttpGet() { _http.end(); }

private:
  NetworkClientSecure _secure;
  NetworkClient _plain;
  HTTPClient _http;
};

// The device must be on a Wi-Fi network with internet, and HTTPS needs memory
static bool pullPreconditions(const String &url, const char *&reason, String &message)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    reason = "no_internet";
    message = "No internet: the device is not connected to a Wi-Fi network (access point only)";
    return false;
  }
  if (url.startsWith("https://") && ESP.getFreeHeap() < HTTPS_MIN_FREE_HEAP)
  {
    reason = "not_enough_memory";
    message = "Not enough free memory for a secure download (" + String(ESP.getFreeHeap() / 1024) + " KB free, 60 KB needed)";
    return false;
  }
  return true;
}

static const char *checkStateName(UpdateManager::CheckState state)
{
  switch (state)
  {
  case UpdateManager::CheckState::Checking:
    return "checking";
  case UpdateManager::CheckState::UpToDate:
    return "up_to_date";
  case UpdateManager::CheckState::Available:
    return "available";
  case UpdateManager::CheckState::NoUpdate:
    return "no_update";
  case UpdateManager::CheckState::NeedsManual:
    return "needs_manual";
  case UpdateManager::CheckState::Failed:
    return "failed";
  default:
    return "never";
  }
}

static const char *stateName(UpdateManager::State state, bool pull)
{
  switch (state)
  {
  case UpdateManager::State::Downloading:
    return "downloading";
  case UpdateManager::State::Receiving:
    return pull ? "downloading" : "receiving";
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

void UpdateManager::bootCheck()
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
    Serial.println("OTA: the last update was rolled back");
    ConfigStorage::invalidateSlot(pendingSlot);
    ConfigStorage::setLastSwitch("auto_rollback");
    ConfigStorage::setPendingSlot(-1, "");
    pendingSlot = -1;
  }

  ConfigStorage::SlotRecord record = ConfigStorage::getSlot(_runningSlot);
  bool known = record.valid && memcmp(record.sha256, sha, sizeof(sha)) == 0;
  if (!known && pendingSlot != _runningSlot)
  {
    // Firmware the records don't know, with no OTA install pending: flashed over USB
    Serial.println("OTA: new firmware from USB, resetting slot records");
    record.valid = true;
    record.version = FIRMWARE_VERSION;
    record.target = OTA_TARGET_ID;
    memcpy(record.sha256, sha, sizeof(sha));
    ConfigStorage::setSlot(_runningSlot, record);
    ConfigStorage::invalidateSlot(1 - _runningSlot);
    ConfigStorage::setHomeTarget(OTA_TARGET_ID);
    ConfigStorage::setLastSwitch("usb");
    ConfigStorage::setPendingSlot(-1, "");
  }
  else if (pendingSlot == _runningSlot && !_unconfirmed)
  {
    // Confirmed earlier but the record was not finished
    ConfigStorage::setLastSwitch(pendingReason);
    ConfigStorage::setPendingSlot(-1, "");
  }

  if (ConfigStorage::getHomeTarget().isEmpty())
    ConfigStorage::setHomeTarget(OTA_TARGET_ID);

  Serial.printf("OTA: running slot %d (%s)%s\n", _runningSlot, FIRMWARE_VERSION, _unconfirmed ? ", unconfirmed" : "");

  if (_unconfirmed)
  {
    esp_timer_create_args_t args = {};
    args.callback = healthDeadline;
    args.name = "ota_deadline";
    esp_timer_handle_t timer;
    int64_t remainingUs = (int64_t)HEALTH_DEADLINE_MS * 1000 - esp_timer_get_time();
    if (esp_timer_create(&args, &timer) == ESP_OK)
      esp_timer_start_once(timer, remainingUs > 0 ? remainingUs : 1);
  }
}

void UpdateManager::setupDone()
{
  _setupDone = true;
  _setupDoneAt = millis();
  // While unconfirmed, a hang is caught by the task watchdog, whose reset rolls back
  if (_unconfirmed)
    enableLoopWDT();
}

uint32_t UpdateManager::confirmSecondsLeft() const
{
  if (!_unconfirmed)
    return 0;
  uint32_t now = millis();
  uint32_t healthyAt = _setupDone ? _setupDoneAt + HEALTH_UPTIME_MS : HEALTH_DEADLINE_MS;
  return healthyAt > now ? (healthyAt - now + 999) / 1000 : 0;
}

String UpdateManager::previousVersion()
{
  ConfigStorage::SlotRecord other = ConfigStorage::getSlot(1 - _runningSlot);
  return other.valid ? other.version : String("");
}

bool UpdateManager::rollbackAvailable()
{
  // Hidden after a manual or automatic rollback, a USB flash, and a failed or
  // incomplete update (Update.begin() invalidates the other slot's record).
  // While unconfirmed the last switch is not recorded yet, and rollback is refused anyway.
  return !_unconfirmed && ConfigStorage::getLastSwitch() == "update" && ConfigStorage::getSlot(1 - _runningSlot).valid && Update.canRollBack();
}

int UpdateManager::requestRollback(String &message)
{
  std::lock_guard<std::mutex> guard(_lock);
  if (_state == State::Downloading || _state == State::Receiving || _state == State::Installing || _state == State::Rebooting)
  {
    message = "An update is already running";
    return 409;
  }
  if (_unconfirmed)
  {
    message = "Finishing update, please wait " + String(confirmSecondsLeft()) + " s";
    return 409;
  }
  if (!rollbackAvailable())
  {
    message = "No previous version to roll back to";
    return 409;
  }
  String version = previousVersion();
  if (!Update.rollBack())
  {
    message = "Rollback failed: the previous firmware is not bootable";
    return 500;
  }
  ConfigStorage::setPendingSlot(1 - _runningSlot, "manual_rollback");
  _state = State::Rebooting;
  _pull = false;
  message = "Rolling back to v" + version + ", rebooting";
  return 200;
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

void UpdateManager::begin()
{
  bootCheck();
  _nextCheckAt = millis() + OTA_CHECK_FIRST_DELAY_MS;

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
  if (_unconfirmed)
  {
    if (_setupDone && millis() - _setupDoneAt >= HEALTH_UPTIME_MS)
    {
      esp_ota_mark_app_valid_cancel_rollback();
      String reason;
      ConfigStorage::getPendingSlot(reason);
      ConfigStorage::setLastSwitch(reason.isEmpty() ? String("update") : reason);
      ConfigStorage::setPendingSlot(-1, "");
      _unconfirmed = false;
      disableLoopWDT();
      Serial.println("OTA: firmware confirmed healthy");
    }
  }

  // Scheduled update checks need a network with internet; until then, look again shortly
  if ((int32_t)(millis() - _nextCheckAt) >= 0)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      _nextCheckAt = millis() + OTA_CHECK_INTERVAL_MS + esp_random() % OTA_CHECK_JITTER_MS;
      String message;
      requestCheck(message);
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
  // A pulled update reboots on its own, once the UI has had time to see the result
  if (_state == State::Rebooting && _pull && (int32_t)(millis() - _rebootAt) >= 0)
  {
    Serial.println("Restarting...");
    ESP.restart();
  }
}

bool UpdateManager::isBusy() const
{
  std::lock_guard<std::mutex> guard(_lock);
  return _state == State::Downloading || _state == State::Receiving || _state == State::Installing || _state == State::Rebooting;
}

void UpdateManager::failLocked(const char *reason, const String &message)
{
  mbedtls_sha256_free(&_sha);
  _state = State::Failed;
  _reason = reason;
  _message = message;
  Serial.println("Update failed: " + message);
}

bool UpdateManager::uploadBegin(size_t total, const uint8_t *data, size_t len, const UploadOptions &options, String &reason, String &message)
{
  std::lock_guard<std::mutex> guard(_lock);
  if (_state == State::Downloading || _state == State::Receiving || _state == State::Installing || _state == State::Rebooting)
  {
    reason = "update_running";
    message = "An update is already running";
    return false;
  }
  if (_unconfirmed)
  {
    // The other slot is the rollback target until this firmware confirms itself
    reason = "finishing_update";
    message = "Finishing update, please wait " + String(confirmSecondsLeft()) + " s";
    return false;
  }
  _pull = false;
  return beginLocked(total, data, len, options, reason, message);
}

bool UpdateManager::beginLocked(size_t total, const uint8_t *data, size_t len, const UploadOptions &options, String &reason, String &message)
{
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

  // The inactive slot is being overwritten: its record no longer holds
  ConfigStorage::invalidateSlot(slotOf(esp_ota_get_next_update_partition(NULL)));

  _state = State::Receiving;
  _total = total;
  _received = 0;
  _options = options;
  _lastDataAt = millis();
  _reason = "";
  _message = "";
  memset(_info, 0, sizeof(_info));
  mbedtls_sha256_init(&_sha);
  mbedtls_sha256_starts(&_sha, 0);
  Serial.printf("Update started: %u bytes%s\n", (unsigned)total, _pull ? " (pulled)" : "");
  return true;
}

bool UpdateManager::uploadWrite(const uint8_t *data, size_t len, size_t index)
{
  std::lock_guard<std::mutex> guard(_lock);
  return writeLocked(data, len, index);
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
  return finishLocked(nullptr);
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
               badSignature ? "This file isn't signed for this project." : String("Verification failed: ") + Update.errorString());
    return false;
  }

  // Record what the new slot holds; it stays pending until it confirms itself
  const esp_partition_t *installed = esp_ota_get_boot_partition();
  ConfigStorage::SlotRecord record;
  record.valid = esp_partition_get_sha256(installed, record.sha256) == ESP_OK;
  const uint8_t *v = _info + INFO_OFF_VERSION;
  record.version = String(readU16(v)) + "." + String(readU16(v + 2)) + "." + String(readU16(v + 4));
  record.target = readText(_info + INFO_OFF_TARGET);
  ConfigStorage::setSlot(slotOf(installed), record);
  ConfigStorage::setPendingSlot(slotOf(installed), "update");

  Serial.printf("Update installed (key %s), rebooting\n", _verifier.matchedKeyId() ? _verifier.matchedKeyId() : "?");
  _state = State::Rebooting;
  _rebootAt = millis() + PULL_REBOOT_DELAY_MS;
  return true;
}

void UpdateManager::uploadCancel()
{
  std::lock_guard<std::mutex> guard(_lock);
  if (_state != State::Receiving || _pull)
    return;
  Update.abort();
  failLocked("upload_cancelled", "Upload cancelled");
}

void UpdateManager::fillStatus(JsonObject out)
{
  std::lock_guard<std::mutex> guard(_lock);
  out["state"] = stateName(_state, _pull);
  out["source"] = _pull ? "pull" : "upload";
  out["progress"] = _total ? (int)((uint64_t)_received * 100 / _total) : 0;
  out["confirming"] = (bool)_unconfirmed;
  if (_unconfirmed)
    out["confirm_in_s"] = confirmSecondsLeft();
  if (_state == State::Failed)
  {
    out["error"] = _reason;
    out["message"] = _message;
  }

  JsonObject check = out["check"].to<JsonObject>();
  check["state"] = checkStateName(_check);
  if (_check == CheckState::Available)
  {
    check["version"] = _availableVersion;
    check["notes"] = _availableNotes;
  }
  if (!_checkMessage.isEmpty())
  {
    check["error"] = _checkReason;
    check["message"] = _checkMessage;
  }
}

void UpdateManager::checkFailed(CheckState state, const char *reason, const String &message)
{
  std::lock_guard<std::mutex> guard(_lock);
  _check = state;
  _checkReason = reason;
  _checkMessage = message;
  Serial.println("Update check: " + message);
}

int UpdateManager::requestCheck(String &message)
{
  {
    std::lock_guard<std::mutex> guard(_lock);
    if (_check == CheckState::Checking)
    {
      message = "Already checking for updates";
      return 409;
    }
    if (_state == State::Downloading || _state == State::Receiving || _state == State::Installing || _state == State::Rebooting)
    {
      message = "An update is already running";
      return 409;
    }
    _check = CheckState::Checking;
    _checkReason = "";
    _checkMessage = "";
  }
  if (xTaskCreate(checkTask, "ota_check", PULL_TASK_STACK, this, 1, nullptr) != pdPASS)
  {
    message = "Could not start the update check";
    checkFailed(CheckState::Failed, "not_enough_memory", message);
    return 500;
  }
  message = "Checking for updates";
  return 202;
}

void UpdateManager::checkTask(void *self)
{
  ((UpdateManager *)self)->runCheck();
  vTaskDelete(nullptr);
}

void UpdateManager::runCheck()
{
  String url = OTA_MANIFEST_URL;
  const char *reason;
  String message;
  if (!allowedUrl(url))
    return checkFailed(CheckState::Failed, "network_failure", "The manifest URL must use HTTPS");
  if (!pullPreconditions(url, reason, message))
    return checkFailed(CheckState::Failed, reason, message);

  Serial.println("Update check: " + url);
  JsonDocument doc;
  {
    HttpGet get;
    int code = get.open(url);
    if (code != HTTP_CODE_OK)
      return checkFailed(CheckState::Failed, "network_failure",
                         code < 0 ? "Could not reach the update server: " + HTTPClient::errorToString(code) : "The update server answered " + String(code));
    if (get.http().getSize() > MANIFEST_MAX_SIZE)
      return checkFailed(CheckState::Failed, "network_failure", "The update manifest is too large");
    // Unknown fields are ignored
    DeserializationError error = deserializeJson(doc, get.http().getStream());
    if (error)
      return checkFailed(CheckState::Failed, "network_failure", String("The update manifest is invalid: ") + error.c_str());
  }

  int schema = doc["schema"] | 0;
  if (schema < 1)
    return checkFailed(CheckState::Failed, "network_failure", "The update manifest is invalid");
  if (schema > MANIFEST_SCHEMA)
    return checkFailed(CheckState::NeedsManual, "manifest_too_new", "This update needs a manual install");
  if (strcmp(doc["project"] | "", PROJECT_SLUG) != 0)
    return checkFailed(CheckState::Failed, "wrong_project", "The update manifest is for another project");

  uint16_t version[3];
  const char *versionText = doc["version"];
  if (!parseVersion(versionText, version))
    return checkFailed(CheckState::Failed, "network_failure", "The update manifest is invalid");

  JsonObject entry = doc["targets"][OTA_TARGET_ID];
  if (entry.isNull())
    return checkFailed(CheckState::NoUpdate, "wrong_target", "No update for this device (" OTA_TARGET_ID ")");

  // Pull never downgrades or reinstalls
  if (compareVersions(version[0], version[1], version[2]) <= 0)
  {
    std::lock_guard<std::mutex> guard(_lock);
    _check = CheckState::UpToDate;
    Serial.printf("Update check: up to date (latest v%s)\n", versionText);
    return;
  }

  String fileUrl = entry["url"] | "";
  size_t fileSize = entry["size"] | 0;
  uint8_t sha[32];
  if (!allowedUrl(fileUrl) || fileSize <= INFO_SIZE + SIGNATURE_SIZE || !parseHex(entry["sha256"], sha, sizeof(sha)))
    return checkFailed(CheckState::Failed, "network_failure", "The update manifest is invalid");

  std::lock_guard<std::mutex> guard(_lock);
  _check = CheckState::Available;
  _availableVersion = versionText;
  _availableNotes = truncateUtf8(doc["notes"] | "", NOTES_MAX_SIZE);
  _availableUrl = fileUrl;
  _availableSize = fileSize;
  memcpy(_availableSha, sha, sizeof(sha));
  Serial.printf("Update check: v%s available\n", versionText);
}

int UpdateManager::requestInstall(String &message)
{
  {
    std::lock_guard<std::mutex> guard(_lock);
    if (_state == State::Downloading || _state == State::Receiving || _state == State::Installing || _state == State::Rebooting)
    {
      message = "An update is already running";
      return 409;
    }
    if (_unconfirmed)
    {
      message = "Finishing update, please wait " + String(confirmSecondsLeft()) + " s";
      return 409;
    }
    if (_check != CheckState::Available)
    {
      message = _check == CheckState::Checking ? "Still checking for updates" : "No update available";
      return 409;
    }
    _state = State::Downloading;
    _pull = true;
    _total = _availableSize;
    _received = 0;
    _reason = "";
    _message = "";
    message = "Downloading v" + _availableVersion;
  }
  if (xTaskCreate(downloadTask, "ota_download", PULL_TASK_STACK, this, 1, nullptr) != pdPASS)
  {
    std::lock_guard<std::mutex> guard(_lock);
    failLocked("not_enough_memory", "Could not start the download");
    message = _message;
    return 500;
  }
  return 202;
}

void UpdateManager::downloadTask(void *self)
{
  ((UpdateManager *)self)->runDownload();
  vTaskDelete(nullptr);
}

void UpdateManager::runDownload()
{
  String url;
  size_t size;
  uint8_t expectedSha[32];
  {
    std::lock_guard<std::mutex> guard(_lock);
    url = _availableUrl;
    size = _availableSize;
    memcpy(expectedSha, _availableSha, sizeof(expectedSha));
  }

  const char *reason;
  String message;
  if (!pullPreconditions(url, reason, message))
  {
    std::lock_guard<std::mutex> guard(_lock);
    return failLocked(reason, message);
  }

  uint8_t *buffer = (uint8_t *)malloc(DOWNLOAD_CHUNK);
  if (!buffer)
  {
    std::lock_guard<std::mutex> guard(_lock);
    return failLocked("not_enough_memory", "Not enough free memory for the download");
  }

  Serial.println("Downloading " + url);
  HttpGet get;
  int code = get.open(url);
  int length = code == HTTP_CODE_OK ? get.http().getSize() : 0;
  if (code != HTTP_CODE_OK || (length >= 0 && (size_t)length != size))
  {
    free(buffer);
    std::lock_guard<std::mutex> guard(_lock);
    if (code != HTTP_CODE_OK)
      return failLocked("network_failure", code < 0 ? "Download failed: " + HTTPClient::errorToString(code) : "The update server answered " + String(code));
    return failLocked("verification_failed", "The download doesn't match the release manifest");
  }

  // Same path as a manual upload: info-block checks, then Update.end() verifies the signature
  NetworkClient *stream = get.http().getStreamPtr();
  size_t received = 0;
  uint32_t lastDataAt = millis();
  bool ok = true;
  while (ok && received < size)
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
    size_t n = stream->readBytes(buffer, std::min(std::min(available, (size_t)DOWNLOAD_CHUNK), size - received));
    if (n == 0)
      continue;
    lastDataAt = millis();

    std::lock_guard<std::mutex> guard(_lock);
    if (received == 0)
    {
      UploadOptions options; // pull never downgrades or changes the target
      String beginReason;
      ok = _state == State::Downloading && beginLocked(size, buffer, n, options, beginReason, message);
    }
    ok = ok && writeLocked(buffer, n, received);
    received += n;
  }
  free(buffer);

  if (ok)
  {
    std::lock_guard<std::mutex> guard(_lock);
    finishLocked(expectedSha);
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
