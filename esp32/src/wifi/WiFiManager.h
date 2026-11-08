#pragma once

#include <Arduino.h>
#include <atomic>
#include <mutex>
#include <vector>

struct DeviceSettings;

/**
 * @brief Wi-Fi: the saved network, the access point (SoftAP), scans and connecting to new networks.
 *
 * WPA2-Enterprise credentials are stored in the password field as "username|password".
 */
class WiFiManager
{
public:
  /**
   * @brief State of the latest background connection attempt.
   */
  enum class AttemptState : uint8_t
  {
    None,
    Connecting,
    Succeeded,
    Failed
  };

  /**
   * @brief The latest background connection attempt.
   */
  struct Attempt
  {
    AttemptState state;
    String ssid;
  };

  /**
   * @brief State of the background network scan.
   */
  enum class ScanState : uint8_t
  {
    Scanning, ///< Running; ask again shortly
    Ready,    ///< Results returned
    Busy,     ///< Can't scan while connecting to a network
    Failed    ///< The scan could not be started
  };

  /**
   * @brief One network found by a scan.
   */
  struct Network
  {
    String ssid; ///< "" for a hidden network, unless the device is connected to it
    bool hidden;
    String bssid;
    int32_t rssi;
    int32_t channel;
    const char *encryption;
    bool connected; ///< This is the access point the device is connected to
  };

  /**
   * @brief Connects with the saved network (3 attempts), starts the access point when it is
   * enabled or the connection failed, then starts mDNS. Call from setup().
   */
  void begin();

  /**
   * @brief Starts connecting to a network in a background task, so the web server keeps running.
   * The network is saved once the connection succeeds. If it fails, the device goes back to the
   * saved network, or starts the access point when that fails too.
   * @return False if a connection attempt is already running or the task could not start.
   */
  bool startConnect(const String &ssid, const String &password);

  /**
   * @brief True while a background connection attempt is running.
   */
  bool isConnecting() const { return _attemptState == AttemptState::Connecting; }

  /**
   * @brief The latest background connection attempt.
   */
  Attempt attempt() const;

  /**
   * @brief Disconnects and forgets the saved network.
   */
  void forgetNetwork();

  /**
   * @brief Returns the networks found by the background scan, starting a scan when there are no fresh results.
   * @param networks Filled, strongest first, when the result is Ready.
   */
  ScanState scan(std::vector<Network> &networks);

  /**
   * @brief True while a background scan is running.
   */
  bool isScanning() const;

private:
  std::atomic<AttemptState> _attemptState{AttemptState::None};
  mutable std::mutex _attemptLock; // Guards _attemptSsid and _attemptPassword
  String _attemptSsid;
  String _attemptPassword;
  uint32_t _scanStartedAt = 0;

  /**
   * @brief Connects to a network and waits up to 10 s. Saves the network on success.
   */
  bool connect(const String &ssid, const String &password);

  /**
   * @brief After a failed attempt: reconnects to the saved network, or starts the access point
   * if that fails too, so the device stays reachable.
   */
  void restoreConnection();
  void startAccessPoint(const DeviceSettings &settings);
  void startMdns();
  static void connectTask(void *arg);
};
