#pragma once

#include <Arduino.h>

/**
 * @brief Reads the release manifest to find a newer version for this build target.
 *
 * Manifest: {schema: 1, project, version, notes, targets: {<target>: {url, size, sha256}}}.
 * Unknown fields are ignored; a newer schema means the update needs a manual install.
 */
namespace UpdateCheck
{
/**
 * @brief Result of the last check.
 */
enum class State : uint8_t
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
 * @brief The newer release a check found.
 */
struct Release
{
  String version;
  String notes;
  String url;
  size_t size = 0;
  uint8_t sha256[32] = {0};
};

struct Result
{
  State state = State::Failed;
  const char *error = "";
  String message;
  Release release; ///< Set when state is Available
};

/**
 * @brief True when this dev build checks a local manifest instead of the GitHub release.
 */
bool usesDevManifest();

/**
 * @brief Fetches and reads the manifest. Blocks for the network: run it in a background task.
 */
Result run();

/**
 * @brief API name of a state.
 */
const char *stateName(State state);
} // namespace UpdateCheck
