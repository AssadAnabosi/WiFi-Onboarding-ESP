#ifndef WiFiManager_h
#define WiFiManager_h

#include <WiFi.h>

#include <Arduino.h>
#include <atomic>

/**
 * @brief Manages WiFi connections and Access Point (AP) setup.
 */
class WiFiManager {
public:
  /**
     * @brief Attempts to automatically connect to a previously known WiFi network.
     * @return True if the connection is successful, false otherwise.
     */
  bool autoConnect();

  /**
     * @brief Attempts to connect to a specified WiFi network.
     * @param ssid The SSID of the network.
     * @param password The password for the network.
     * @return True if the connection is successful, false otherwise.
     */
  bool connectToNetwork(const char *ssid, const char *password);

  /**
   * @brief Result of the latest background connection attempt.
   */
  enum class AttemptState : uint8_t
  {
    None,
    Connecting,
    Succeeded,
    Failed
  };

  /**
   * @brief Starts connecting to a network in a background task, so the web server keeps running.
   * @param ssid The SSID of the network.
   * @param password The password for the network.
   * @return False if a connection attempt is already running.
   */
  bool startConnect(const String &ssid, const String &password);

  /**
   * @brief True while a background connection attempt is running.
   */
  bool isConnecting() const { return _attemptState == AttemptState::Connecting; }

  /**
   * @brief State of the latest background connection attempt.
   */
  AttemptState attemptState() const { return _attemptState; }

  /**
   * @brief SSID of the latest background connection attempt.
   */
  const String &attemptSSID() const { return _attemptSSID; }

private:
  std::atomic<AttemptState> _attemptState{AttemptState::None};
  String _attemptSSID;
  String _attemptPassword;

  static void connectTask(void *arg);

  /**
     * @brief Sets up the device as a Soft Access Point (using saved settings, if enabled).
     */
  void setupSoftAP();
};

#endif
