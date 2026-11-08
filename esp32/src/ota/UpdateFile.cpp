#include "UpdateFile.h"

#include "../../ProjectConfig.h"
#include "../../Version.h"

namespace
{
constexpr char INFO_MAGIC[4] = {'W', 'O', 'T', 'A'};
constexpr uint16_t INFO_FORMAT_VERSION = 1;

// Info block field offsets
constexpr size_t OFF_FORMAT = 4;
constexpr size_t OFF_FIRMWARE_LENGTH = 6;
constexpr size_t OFF_VERSION = 10;
constexpr size_t OFF_CHIP = 16;
constexpr size_t OFF_LAYOUT = 18;
constexpr size_t OFF_TARGET = 50;
constexpr size_t OFF_SLUG = 82;
constexpr size_t TEXT_SIZE = 32;

// First byte of an app image; classic ESP32 factory images start with 0xFF padding instead
constexpr uint8_t ESP_IMAGE_MAGIC = 0xE9;
constexpr uint8_t FACTORY_IMAGE_PADDING = 0xFF;

uint16_t readU16(const uint8_t *p)
{
  return p[0] | (p[1] << 8);
}

uint32_t readU32(const uint8_t *p)
{
  return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

String readText(const uint8_t *p)
{
  char text[TEXT_SIZE + 1];
  memcpy(text, p, TEXT_SIZE);
  text[TEXT_SIZE] = '\0';
  return String(text);
}

uint64_t packVersion(uint16_t major, uint16_t minor, uint16_t patch)
{
  return ((uint64_t)major << 32) | ((uint64_t)minor << 16) | patch;
}
} // namespace

String UpdateFile::Info::versionString() const
{
  return String(version[0]) + "." + String(version[1]) + "." + String(version[2]);
}

UpdateFile::Info UpdateFile::parseInfo(const uint8_t *block)
{
  Info info;
  info.magicOk = memcmp(block, INFO_MAGIC, sizeof(INFO_MAGIC)) == 0;
  info.format = readU16(block + OFF_FORMAT);
  info.firmwareLength = readU32(block + OFF_FIRMWARE_LENGTH);
  for (int i = 0; i < 3; i++)
    info.version[i] = readU16(block + OFF_VERSION + i * 2);
  info.chipId = readU16(block + OFF_CHIP);
  memcpy(info.layout, block + OFF_LAYOUT, sizeof(info.layout));
  info.target = readText(block + OFF_TARGET);
  info.slug = readText(block + OFF_SLUG);
  return info;
}

const char *UpdateFile::checkStart(const uint8_t *data, size_t len, size_t total, String &message)
{
  if (len > 0 && data[0] == FACTORY_IMAGE_PADDING)
  {
    message = "This is a factory image for USB flashing, not an update";
    return "factory_image";
  }
  if (len == 0 || data[0] != ESP_IMAGE_MAGIC || total <= INFO_SIZE + SIGNATURE_SIZE)
  {
    message = "This file is not a firmware update";
    return "invalid_file";
  }
  return nullptr;
}

const char *UpdateFile::checkInfo(const Info &info, size_t total, const uint8_t *layoutHash, const Options &options, String &message)
{
  if (!info.magicOk || info.format != INFO_FORMAT_VERSION)
  {
    message = "This file is not an update for this project";
    return "invalid_file";
  }
  if (info.firmwareLength != total - INFO_SIZE - SIGNATURE_SIZE)
  {
    message = "The update file is damaged";
    return "invalid_file";
  }
  if (info.slug != PROJECT_SLUG)
  {
    message = "This file is for another project";
    return "wrong_project";
  }
  if (info.chipId != CONFIG_IDF_FIRMWARE_CHIP_ID)
  {
    message = "This file is for another chip and cannot run on this device";
    return "wrong_target";
  }
  if (!layoutHash || memcmp(info.layout, layoutHash, sizeof(info.layout)) != 0)
  {
    message = "This file is for another flash layout and cannot run on this device";
    return "wrong_target";
  }
  if (info.target != OTA_TARGET_ID && !options.allowTargetChange)
  {
    message = "This file is for " + info.target + ". This device runs " OTA_TARGET_ID;
    return "wrong_target";
  }
  if (compareWithRunning(info.version) <= 0 && !options.allowDowngrade)
  {
    message = "This file is not newer than the installed version";
    return "not_newer";
  }
  return nullptr;
}

bool UpdateFile::parseVersion(const char *text, uint16_t version[3])
{
  unsigned major, minor, patch;
  char extra;
  if (!text || sscanf(text, "%u.%u.%u%c", &major, &minor, &patch, &extra) != 3 ||
      major > 0xFFFF || minor > 0xFFFF || patch > 0xFFFF)
    return false;
  version[0] = major;
  version[1] = minor;
  version[2] = patch;
  return true;
}

int UpdateFile::compareWithRunning(const uint16_t version[3])
{
  uint64_t other = packVersion(version[0], version[1], version[2]);
  uint64_t running = packVersion(FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR, FIRMWARE_VERSION_PATCH);
  return (other > running) - (other < running);
}
