#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <mutex>
#include "OtaVerifier.h"

/**
 * @brief Installs signed firmware updates into the inactive app slot.
 *
 * Update file layout: [firmware][info block, 128 bytes][signature, 512 bytes].
 * The info block is checked before Update.end(), which verifies the signature
 * and switches the boot slot. Only one update runs at a time.
 */
class UpdateManager
{
public:
  enum class State : uint8_t
  {
    Idle,
    Receiving,
    Installing,
    Rebooting,
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
   * @brief Cancels an upload that stopped sending. Call from loop().
   */
  void loop();

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
  size_t _total = 0;
  size_t _received = 0;
  uint32_t _lastDataAt = 0;
  UploadOptions _options;
  uint8_t _info[128];
  uint8_t _layoutHash[32];
  bool _layoutHashValid = false;
  String _reason;
  String _message;
  TrustedKeyVerifier _verifier;

  void failLocked(const char *reason, const String &message);
  bool checkInfoLocked();
};

extern UpdateManager updateManager;
