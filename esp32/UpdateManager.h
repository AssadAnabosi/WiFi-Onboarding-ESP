#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <mutex>
#include <mbedtls/sha256.h>
#include "OtaVerifier.h"

/**
 * @brief Installs signed firmware updates into the inactive app slot.
 *
 * Update file layout: [firmware][info block, 128 bytes][signature, 512 bytes].
 * The info block is checked before Update.end(), which verifies the signature
 * and switches the boot slot. Manual uploads and pulled updates share these
 * checks and one lock: only one update runs at a time.
 */
class UpdateManager
{
public:
  enum class State : uint8_t
  {
    Idle,
    Downloading,
    Receiving,
    Installing,
    Rebooting,
    Failed
  };

  /**
   * @brief Result of the last update check against the release manifest.
   */
  enum class CheckState : uint8_t
  {
    Never,
    Checking,
    UpToDate,
    Available,
    NoUpdate,
    NeedsManual,
    Failed
  };

  /**
   * @brief Options the client confirmed after the browser showed a warning.
   */
  struct UploadOptions
  {
    bool allowDowngrade = false;
    bool allowTargetChange = false;
  };

  /**
   * @brief Installs the signature verifier and reads this device's flash layout. Call from setup().
   */
  void begin();

  /**
   * @brief Call at the end of setup(): starts the health check of an unconfirmed firmware.
   */
  void setupDone();

  /**
   * @brief Runs the health check and cancels an upload that stopped sending. Call from loop().
   */
  void loop();

  /**
   * @brief True while a newly installed firmware has not confirmed itself yet.
   * Reboots, settings changes, factory reset and new updates are refused meanwhile.
   */
  bool isUnconfirmed() const { return _unconfirmed; }

  /**
   * @brief Seconds until the firmware should confirm itself (0 when confirmed).
   */
  uint32_t confirmSecondsLeft() const;

  /**
   * @brief Version in the other slot, or "" when it holds no valid firmware.
   */
  String previousVersion();

  /**
   * @brief SHA-256 of this device's partition table as hex (an update's layout hash must match).
   */
  String layoutHash() const;

  /**
   * @brief True while an update is receiving, installing or about to reboot.
   */
  bool isBusy() const;

  /**
   * @brief Starts a manual upload of `total` bytes, given its first chunk.
   * @return False with `reason`/`message` set if the upload is refused.
   */
  bool uploadBegin(size_t total, const uint8_t *data, size_t len, const UploadOptions &options, String &reason, String &message);

  /**
   * @brief Writes the next chunk of the upload. Returns false once the upload has failed.
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
   * @brief Starts a background check of the release manifest.
   * @return HTTP status for the response (202 started, 409 refused) with `message` set.
   */
  int requestCheck(String &message);

  /**
   * @brief Starts downloading and installing the update the last check found.
   * @return HTTP status for the response (202 started, 409 refused) with `message` set.
   */
  int requestInstall(String &message);

  /**
   * @brief Fills the /api/update/status JSON.
   */
  void fillStatus(JsonObject out);

  /**
   * @brief Reason code and message of the last failure.
   */
  String lastReason();
  String lastMessage();

private:
  mutable std::mutex _lock;
  State _state = State::Idle;
  bool _pull = false;
  uint32_t _rebootAt = 0;
  size_t _total = 0;
  size_t _received = 0;
  uint32_t _lastDataAt = 0;
  UploadOptions _options;
  uint8_t _info[128];
  mbedtls_sha256_context _sha;
  uint8_t _layoutHash[32];
  bool _layoutHashValid = false;
  String _reason;
  String _message;
  TrustedKeyVerifier _verifier;

  volatile bool _unconfirmed = false;
  bool _setupDone = false;
  uint32_t _setupDoneAt = 0;
  int _runningSlot = 0;

  // Update check and the update it found
  CheckState _check = CheckState::Never;
  String _checkReason;
  String _checkMessage;
  String _availableVersion;
  String _availableNotes;
  String _availableUrl;
  size_t _availableSize = 0;
  uint8_t _availableSha[32];
  uint32_t _nextCheckAt = 0;

  void bootCheck();
  void failLocked(const char *reason, const String &message);
  bool beginLocked(size_t total, const uint8_t *data, size_t len, const UploadOptions &options, String &reason, String &message);
  bool writeLocked(const uint8_t *data, size_t len, size_t index);
  bool finishLocked(const uint8_t *expectedSha);
  bool checkInfoLocked();
  void checkFailed(CheckState state, const char *reason, const String &message);
  void runCheck();
  void runDownload();
  static void checkTask(void *self);
  static void downloadTask(void *self);
};

extern UpdateManager updateManager;
