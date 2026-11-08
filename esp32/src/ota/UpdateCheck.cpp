#include "UpdateCheck.h"

#include <ArduinoJson.h>

#include "../../ProjectConfig.h"
#include "../system/BuildInfo.h"
#include "../system/Log.h"
#include "HttpGet.h"
#include "UpdateFile.h"

#if FIRMWARE_DEV_BUILD
#include "../../OtaPublicKey.local.h" // May set OTA_DEV_MANIFEST_URL
#endif

// Dev builds may check a local manifest (utils/keygen.py --dev-manifest-url), plain HTTP allowed
#if FIRMWARE_DEV_BUILD && defined(OTA_DEV_MANIFEST_URL)
#define OTA_MANIFEST_URL OTA_DEV_MANIFEST_URL
#define OTA_MANIFEST_IS_DEV true
#else
#define OTA_MANIFEST_URL "https://github.com/" OTA_GITHUB_OWNER "/" OTA_GITHUB_REPO "/releases/latest/download/manifest.json"
#define OTA_MANIFEST_IS_DEV false
#endif

namespace
{
constexpr int MANIFEST_SCHEMA = 1;
constexpr int MANIFEST_MAX_SIZE = 16384;
constexpr size_t NOTES_MAX_SIZE = 4096;

UpdateCheck::Result fail(UpdateCheck::State state, const char *error, const String &message)
{
  UpdateCheck::Result result;
  result.state = state;
  result.error = error;
  result.message = message;
  LOG("ota", "Update check: %s", message.c_str());
  return result;
}

UpdateCheck::Result invalidManifest()
{
  return fail(UpdateCheck::State::Failed, "network_failure", "The update manifest is invalid");
}

// Cut at most `max` bytes without splitting a UTF-8 character
String truncateUtf8(const String &text, size_t max)
{
  if (text.length() <= max)
    return text;
  size_t end = max;
  while (end > 0 && (text[end] & 0xC0) == 0x80)
    end--;
  return text.substring(0, end);
}

bool parseHex(const char *hex, uint8_t *out, size_t size)
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
} // namespace

bool UpdateCheck::usesDevManifest()
{
  return OTA_MANIFEST_IS_DEV;
}

UpdateCheck::Result UpdateCheck::run()
{
  String url = OTA_MANIFEST_URL;
  const char *error;
  String message;
  if (!HttpGet::isAllowedUrl(url))
    return fail(State::Failed, "network_failure", "The manifest URL must use HTTPS");
  if (!HttpGet::canFetch(url, error, message))
    return fail(State::Failed, error, message);

  LOG("ota", "Update check: %s", url.c_str());
  JsonDocument doc;
  {
    HttpGet get;
    int code = get.open(url);
    if (code != HTTP_CODE_OK)
      return fail(State::Failed, "network_failure",
                  code < 0 ? "Could not reach the update server: " + HTTPClient::errorToString(code)
                           : "The update server answered " + String(code));
    if (get.http().getSize() > MANIFEST_MAX_SIZE)
      return fail(State::Failed, "network_failure", "The update manifest is too large");
    DeserializationError parseError = deserializeJson(doc, get.http().getStream());
    if (parseError)
      return fail(State::Failed, "network_failure", String("The update manifest is invalid: ") + parseError.c_str());
  }

  int schema = doc["schema"] | 0;
  if (schema < 1)
    return invalidManifest();
  if (schema > MANIFEST_SCHEMA)
    return fail(State::NeedsManual, "manifest_too_new", "This update needs a manual install");
  if (strcmp(doc["project"] | "", PROJECT_SLUG) != 0)
    return fail(State::Failed, "wrong_project", "The update manifest is for another project");

  const char *versionText = doc["version"];
  uint16_t version[3];
  if (!UpdateFile::parseVersion(versionText, version))
    return invalidManifest();

  JsonObject entry = doc["targets"][OTA_TARGET_ID];
  if (entry.isNull())
    return fail(State::NoUpdate, "wrong_target", "No update for this device (" OTA_TARGET_ID ")");

  Result result;
  // A pulled update never downgrades or reinstalls
  if (UpdateFile::compareWithRunning(version) <= 0)
  {
    result.state = State::UpToDate;
    LOG("ota", "Update check: up to date (latest v%s)", versionText);
    return result;
  }

  Release &release = result.release;
  release.url = entry["url"] | "";
  release.size = entry["size"] | 0;
  if (!HttpGet::isAllowedUrl(release.url) || release.size <= UpdateFile::INFO_SIZE + UpdateFile::SIGNATURE_SIZE ||
      !parseHex(entry["sha256"], release.sha256, sizeof(release.sha256)))
    return invalidManifest();
  release.version = versionText;
  release.notes = truncateUtf8(doc["notes"] | "", NOTES_MAX_SIZE);
  result.state = State::Available;
  LOG("ota", "Update check: v%s available", versionText);
  return result;
}

const char *UpdateCheck::stateName(State state)
{
  switch (state)
  {
  case State::Checking:
    return "checking";
  case State::UpToDate:
    return "up_to_date";
  case State::Available:
    return "available";
  case State::NoUpdate:
    return "no_update";
  case State::NeedsManual:
    return "needs_manual";
  case State::Failed:
    return "failed";
  default:
    return "never";
  }
}
