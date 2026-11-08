#pragma once

#include <Arduino.h>

/**
 * @brief Layout and rules of a signed update file, as written by utils/sign.py:
 * [firmware][info block, 128 bytes][DER ECDSA P-256 signature, zero-padded to 512 bytes].
 *
 * The info block (little-endian, text zero-padded) names the project, chip, flash layout,
 * build target and version. Its rules are checked before Update.end() verifies the signature.
 */
namespace UpdateFile
{
constexpr size_t INFO_SIZE = 128;
constexpr size_t SIGNATURE_SIZE = 512;

/**
 * @brief Checks the client confirmed after the browser warned. Pulled updates never set them.
 */
struct Options
{
  bool allowDowngrade = false;
  bool allowTargetChange = false;
};

/**
 * @brief The fields of an info block.
 */
struct Info
{
  bool magicOk = false;
  uint16_t format = 0;
  uint32_t firmwareLength = 0;
  uint16_t version[3] = {0, 0, 0};
  uint16_t chipId = 0;
  uint8_t layout[32] = {0};
  String target;
  String slug;

  String versionString() const;
};

/**
 * @brief Reads an info block (INFO_SIZE bytes).
 */
Info parseInfo(const uint8_t *block);

/**
 * @brief Checks the first chunk of a file of `total` bytes, before anything is written to flash.
 * @return nullptr if it may be an update, else the error code with `message` set.
 */
const char *checkStart(const uint8_t *data, size_t len, size_t total, String &message);

/**
 * @brief Checks an info block against this device and firmware.
 * @param layoutHash This device's partition table hash, or nullptr when it could not be read.
 * @return nullptr if the file may be installed, else the error code with `message` set.
 */
const char *checkInfo(const Info &info, size_t total, const uint8_t *layoutHash, const Options &options, String &message);

/**
 * @brief Parses "X.Y.Z" (each part up to 65535).
 */
bool parseVersion(const char *text, uint16_t version[3]);

/**
 * @brief Compares a version with the running firmware's: <0 older, 0 same, >0 newer.
 */
int compareWithRunning(const uint16_t version[3]);
} // namespace UpdateFile
