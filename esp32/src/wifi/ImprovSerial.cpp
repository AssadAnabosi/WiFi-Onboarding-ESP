#include "ImprovSerial.h"

#include <WiFi.h>
#include <algorithm>
#include <sdkconfig.h>
#include <vector>

#include "../../ProjectConfig.h"
#include "../ota/UpdateManager.h"
#include "../storage/ConfigStorage.h"
#include "../system/BuildInfo.h"
#include "../system/Log.h"
#include "WiFiManager.h"

namespace
{
constexpr uint8_t HEADER[] = {'I', 'M', 'P', 'R', 'O', 'V'};
constexpr uint8_t IMPROV_VERSION = 1;

// Packet types
constexpr uint8_t TYPE_CURRENT_STATE = 0x01;
constexpr uint8_t TYPE_ERROR_STATE = 0x02;
constexpr uint8_t TYPE_RPC = 0x03;
constexpr uint8_t TYPE_RPC_RESULT = 0x04;

// States
constexpr uint8_t STATE_READY = 0x02;
constexpr uint8_t STATE_PROVISIONING = 0x03;
constexpr uint8_t STATE_PROVISIONED = 0x04;

// Errors
constexpr uint8_t ERROR_NONE = 0x00;
constexpr uint8_t ERROR_INVALID_RPC = 0x01;
constexpr uint8_t ERROR_UNKNOWN_RPC = 0x02;
constexpr uint8_t ERROR_UNABLE_TO_CONNECT = 0x03;
constexpr uint8_t ERROR_UNKNOWN = 0xFF;

// RPC commands
constexpr uint8_t RPC_WIFI_SETTINGS = 0x01;
constexpr uint8_t RPC_CURRENT_STATE = 0x02;
constexpr uint8_t RPC_DEVICE_INFO = 0x03;
constexpr uint8_t RPC_SCAN = 0x04;

constexpr uint32_t TASK_STACK = 6144;
constexpr uint32_t IDLE_DELAY_MS = 10;
constexpr uint32_t SCAN_POLL_MS = 100;
constexpr uint32_t SCAN_TIMEOUT_MS = 20000;

// "esp32s3" -> "ESP32-S3", the chip family names ESP Web Tools uses
String chipFamily()
{
  String target = CONFIG_IDF_TARGET;
  target.toUpperCase();
  if (target.length() > 5)
    target = target.substring(0, 5) + "-" + target.substring(5);
  return target;
}

String deviceUrl()
{
  return "http://" + WiFi.localIP().toString();
}
} // namespace

void ImprovSerial::begin()
{
  if (xTaskCreate(task, "improv", TASK_STACK, this, 1, nullptr) != pdPASS)
    LOG("wifi", "Failed to start Improv serial");
}

void ImprovSerial::task(void *arg)
{
  static_cast<ImprovSerial *>(arg)->run();
}

void ImprovSerial::run()
{
  for (;;)
  {
    bool idle = true;
    while (Serial.available() > 0)
    {
      readByte(static_cast<uint8_t>(Serial.read()));
      idle = false;
    }
    continueProvisioning();
    if (idle)
      vTaskDelay(pdMS_TO_TICKS(IDLE_DELAY_MS));
  }
}

void ImprovSerial::readByte(uint8_t byte)
{
  // Look for the header byte by byte; anything else on the port (e.g. typed text) is skipped
  if (_received < sizeof(HEADER))
  {
    if (byte == HEADER[_received])
      _packet[_received++] = byte;
    else
      _received = byte == HEADER[0] ? 1 : 0;
    return;
  }
  if (_received == sizeof(HEADER) && byte != IMPROV_VERSION)
  {
    _received = byte == HEADER[0] ? 1 : 0;
    return;
  }
  _packet[_received++] = byte;
  if (_received < HEADER_SIZE)
    return;
  size_t total = HEADER_SIZE + _packet[8] + 1;
  if (_received < total)
    return;
  handlePacket();
  _received = 0;
}

void ImprovSerial::handlePacket()
{
  size_t length = _packet[8];
  uint8_t checksum = 0;
  for (size_t i = 0; i < HEADER_SIZE + length; i++)
    checksum += _packet[i];
  if (checksum != _packet[HEADER_SIZE + length])
  {
    sendError(ERROR_INVALID_RPC);
    return;
  }
  if (_packet[7] == TYPE_RPC)
    handleRpc(_packet + HEADER_SIZE, length);
}

void ImprovSerial::handleRpc(const uint8_t *data, size_t length)
{
  if (length < 2 || data[1] + 2u > length)
  {
    sendError(ERROR_INVALID_RPC);
    return;
  }
  // The spec asks for a cleared error before each answer
  sendError(ERROR_NONE);
  const uint8_t *args = data + 2;
  size_t argsLength = data[1];
  switch (data[0])
  {
  case RPC_WIFI_SETTINGS:
    handleWifiSettings(args, argsLength);
    break;
  case RPC_CURRENT_STATE:
    sendCurrentState(true);
    break;
  case RPC_DEVICE_INFO:
    sendDeviceInfo();
    break;
  case RPC_SCAN:
    sendNetworks();
    break;
  default:
    sendError(ERROR_UNKNOWN_RPC);
    break;
  }
}

void ImprovSerial::handleWifiSettings(const uint8_t *data, size_t length)
{
  // <ssid length> <ssid> <password length> <password>
  if (length < 1 || data[0] + 2u > length || data[0] + 2u + data[data[0] + 1] > length)
  {
    sendError(ERROR_INVALID_RPC);
    return;
  }
  if (_pending || _provisioning)
  {
    sendError(ERROR_UNKNOWN);
    return;
  }
  size_t ssidLength = data[0];
  size_t passwordLength = data[ssidLength + 1];
  _pendingSsid = String(reinterpret_cast<const char *>(data + 1), ssidLength);
  _pendingPassword = String(reinterpret_cast<const char *>(data + ssidLength + 2), passwordLength);
  _pending = true;
  LOG("wifi", "Improv: received a network to join (%s)", _pendingSsid.c_str());
  sendState(STATE_PROVISIONING);
  continueProvisioning();
}

void ImprovSerial::continueProvisioning()
{
  if (_pending)
  {
    // Wait until the saved network has been tried, and for a scan or an attempt from the web page
    if (!_wifi.isReady() || _wifi.isScanning() || _wifi.isConnecting())
      return;
    _pending = false;
    String ssid = _pendingSsid;
    String password = _pendingPassword;
    _pendingPassword = "";
    if (_updates.isBusy() || !_wifi.startConnect(ssid, password))
    {
      LOG("wifi", "Improv: could not start connecting");
      sendError(ERROR_UNKNOWN);
      sendCurrentState(false);
      return;
    }
    _provisioning = true;
    return;
  }

  if (!_provisioning || _wifi.isConnecting())
    return;
  _provisioning = false;
  if (_wifi.attempt().state == WiFiManager::AttemptState::Succeeded)
  {
    sendState(STATE_PROVISIONED);
    String url = deviceUrl();
    sendResult(RPC_WIFI_SETTINGS, &url, 1);
  }
  else
  {
    sendError(ERROR_UNABLE_TO_CONNECT);
    sendState(STATE_READY);
  }
}

void ImprovSerial::sendCurrentState(bool withUrl)
{
  if (_pending || _provisioning)
  {
    sendState(STATE_PROVISIONING);
    return;
  }
  if (WiFi.status() != WL_CONNECTED)
  {
    sendState(STATE_READY);
    return;
  }
  sendState(STATE_PROVISIONED);
  // Clients expect the same answer as after provisioning: the address to visit
  if (withUrl)
  {
    String url = deviceUrl();
    sendResult(RPC_CURRENT_STATE, &url, 1);
  }
}

void ImprovSerial::sendDeviceInfo()
{
  // Firmware name, firmware version, chip, device name
  String info[] = {PROJECT_SLUG, FIRMWARE_VERSION, chipFamily(), ConfigStorage::loadSettings().hostname};
  sendResult(RPC_DEVICE_INFO, info, 4);
}

void ImprovSerial::sendNetworks()
{
  std::vector<WiFiManager::Network> networks;
  bool ready = false;
  // No scan while the saved network is tried, a connection attempt runs or an update downloads
  if (_wifi.isReady() && !_wifi.isConnecting() && !_updates.isBusy())
  {
    uint32_t startedAt = millis();
    for (;;)
    {
      WiFiManager::ScanState state = _wifi.scan(networks);
      if (state == WiFiManager::ScanState::Ready)
      {
        ready = true;
        break;
      }
      if (state != WiFiManager::ScanState::Scanning || millis() - startedAt > SCAN_TIMEOUT_MS)
        break;
      vTaskDelay(pdMS_TO_TICKS(SCAN_POLL_MS));
    }
  }

  // One result per network name (the strongest, as the list is sorted), then an empty result
  if (ready)
  {
    std::vector<String> sent;
    for (const WiFiManager::Network &network : networks)
    {
      if (network.hidden || std::find(sent.begin(), sent.end(), network.ssid) != sent.end())
        continue;
      sent.push_back(network.ssid);
      String result[] = {network.ssid, String(network.rssi), strcmp(network.encryption, "OPEN") == 0 ? "NO" : "YES"};
      sendResult(RPC_SCAN, result, 3);
    }
  }
  sendResult(RPC_SCAN, nullptr, 0);
}

void ImprovSerial::sendState(uint8_t state)
{
  sendPacket(TYPE_CURRENT_STATE, &state, 1);
}

void ImprovSerial::sendError(uint8_t error)
{
  sendPacket(TYPE_ERROR_STATE, &error, 1);
}

void ImprovSerial::sendResult(uint8_t command, const String *strings, size_t count)
{
  // <command> <length> then <string length> <string> for each string
  uint8_t data[255];
  size_t length = 2;
  for (size_t i = 0; i < count; i++)
  {
    size_t stringLength = min<size_t>(strings[i].length(), 255);
    if (length + 1 + stringLength > sizeof(data))
      break;
    data[length++] = stringLength;
    memcpy(data + length, strings[i].c_str(), stringLength);
    length += stringLength;
  }
  data[0] = command;
  data[1] = length - 2;
  sendPacket(TYPE_RPC_RESULT, data, length);
}

void ImprovSerial::sendPacket(uint8_t type, const uint8_t *data, size_t length)
{
  // One write, so the packet never interleaves with a log line from another task
  uint8_t packet[MAX_PACKET + 1];
  memcpy(packet, HEADER, sizeof(HEADER));
  packet[6] = IMPROV_VERSION;
  packet[7] = type;
  packet[8] = length;
  memcpy(packet + HEADER_SIZE, data, length);
  uint8_t checksum = 0;
  for (size_t i = 0; i < HEADER_SIZE + length; i++)
    checksum += packet[i];
  packet[HEADER_SIZE + length] = checksum;
  packet[HEADER_SIZE + length + 1] = '\n';
  Serial.write(packet, HEADER_SIZE + length + 2);
}
