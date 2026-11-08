#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <mutex>

#include "OtaVerifier.h"
#include "UpdateCheck.h"
#include "UpdateFile.h"

class FirmwareSlots;

/**
 * @brief The answer to an update request: an HTTP status, plus an error code and a message.
 */
struct UpdateReply
{
  int status = 200;
  String error;
  String message;
};

/**
 * @brief Installs signed firmware updates into the inactive app slot.
 *
 * Manual uploads and pulled downloads share one state machine, one lock and the same
 * checks: the info-block rules (UpdateFile), then Update.end() verifies the signature and
 * switches the boot slot. Only one update runs at a time. Update checks and downloads run
 * in background tasks so the web server stays responsive.
 */
class UpdateManager
{
public:
  explicit UpdateManager(FirmwareSlots &slots)
      : _slots(slots) {}

  /**
   * @brief Installs the signature verifier and reads this device's flash layout. Call from setup().
   */
  void begin();

  /**
   * @brief Runs scheduled update checks and cancels an upload that stopped sending. Call from loop().
   */
  void loop();

  /**
   * @brief True while an update is downloading, receiving, installing or about to reboot.
   */
  bool isBusy() const;

  /**
   * @brief SHA-256 of this device's partition table as hex (an update's layout hash must match).
   */
  String layoutHash() const;

  // ----- Manual upload, chunk by chunk -----

  /**
   * @brief Starts a manual upload of `total` bytes, given its first chunk.
   */
  UpdateReply uploadBegin(size_t total, const uint8_t *data, size_t len, const UpdateFile::Options &options);

  /**
   * @brief Writes the next chunk. Returns false once the upload has failed (see lastFailure()).
   */
  bool uploadWrite(const uint8_t *data, size_t len, size_t index);

  /**
   * @brief Checks the info block and installs the update. Returns true if the device should reboot.
   */
  bool uploadFinish();

  /**
   * @brief Cancels a running upload (browser disconnected or stopped sending).
   */
  void uploadCancel();

  /**
   * @brief Why the last update failed (status 400).
   */
  UpdateReply lastFailure() const;

  // ----- Pulled updates and rollback -----

  /**
   * @brief Starts a background check of the release manifest (202 started, 409 refused).
   */
  UpdateReply requestCheck();

  /**
   * @brief Starts downloading and installing the update the last check found (202 started, 409 refused).
   */
  UpdateReply requestInstall();

  /**
   * @brief Switches the boot slot back to the previous firmware; the caller reboots on 200.
   */
  UpdateReply requestRollback();

  /**
   * @brief Fills the /api/update/status JSON.
   */
  void fillStatus(JsonObject out) const;

private:
  enum class State : uint8_t
  {
    Idle,
    Downloading, ///< A pulled update is connecting, before its first chunk
    Receiving,   ///< Writing chunks into the inactive slot (upload or download)
    Installing,
    Rebooting,
    Failed
  };

  FirmwareSlots &_slots;
  TrustedKeyVerifier _verifier;
  uint8_t _layoutHash[32];
  bool _layoutHashValid = false;
  uint32_t _nextCheckAt = 0;

  // Everything below is guarded by _lock
  mutable std::mutex _lock;
  State _state = State::Idle;
  bool _pull = false;
  size_t _total = 0;
  size_t _received = 0;
  uint32_t _lastDataAt = 0;
  UpdateFile::Options _options;
  uint8_t _info[UpdateFile::INFO_SIZE];
  mbedtls_sha256_context _sha;
  String _error;
  String _message;
  UpdateCheck::State _check = UpdateCheck::State::Never;
  String _checkError;
  String _checkMessage;
  UpdateCheck::Release _available;

  bool isBusyLocked() const;
  bool refuseLocked(UpdateReply &reply) const;
  void failLocked(const char *error, const String &message);
  bool beginLocked(size_t total, const uint8_t *data, size_t len, const UpdateFile::Options &options);
  bool writeLocked(const uint8_t *data, size_t len, size_t index);
  bool finishLocked(const uint8_t *expectedSha);
  void runCheck();
  void runDownload();
  static const char *stateName(State state, bool pull);
  static void checkTask(void *self);
  static void downloadTask(void *self);
};
