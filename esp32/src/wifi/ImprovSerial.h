#pragma once

#include <Arduino.h>

class WiFiManager;
class UpdateManager;

/**
 * @brief Improv Wi-Fi over the USB serial port (https://www.improv-wifi.com/serial/).
 *
 * Lets the browser installer (ESP Web Tools) send Wi-Fi credentials right after flashing,
 * and offers "Change Wi-Fi" for a device that is already connected. Packets share the
 * serial port with the log: each packet is written in one piece and ends with a newline.
 *
 * Supported RPCs: send Wi-Fi settings, current state, device info, scanned networks.
 * Credentials go through WiFiManager::startConnect(), so they are saved only once the
 * connection works, exactly like a connection made from the web interface.
 */
class ImprovSerial
{
public:
  ImprovSerial(WiFiManager &wifi, UpdateManager &updates)
      : _wifi(wifi), _updates(updates) {}

  /**
   * @brief Starts the background task that reads the serial port. Call early in setup(), before
   * WiFiManager::begin(), so the installer gets answers while the saved network is tried.
   */
  void begin();

private:
  static constexpr size_t HEADER_SIZE = 9; // "IMPROV", version, type, length
  static constexpr size_t MAX_PACKET = HEADER_SIZE + 255 + 1;

  WiFiManager &_wifi;
  UpdateManager &_updates;

  uint8_t _packet[MAX_PACKET];
  size_t _received = 0;

  // Credentials waiting for WiFiManager to be ready, and the attempt they started
  bool _pending = false;
  bool _provisioning = false;
  String _pendingSsid;
  String _pendingPassword;

  static void task(void *arg);
  void run();
  void readByte(uint8_t byte);
  void handlePacket();
  void handleRpc(const uint8_t *data, size_t length);
  void handleWifiSettings(const uint8_t *data, size_t length);
  void sendCurrentState(bool withUrl);
  void sendDeviceInfo();
  void sendNetworks();
  void continueProvisioning();

  void sendState(uint8_t state);
  void sendError(uint8_t error);
  void sendResult(uint8_t command, const String *strings, size_t count);
  void sendPacket(uint8_t type, const uint8_t *data, size_t length);
};
