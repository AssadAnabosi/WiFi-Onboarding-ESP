#pragma once

#include "../../Version.h"

/**
 * @brief Build identity reported by the API and shown in the UI.
 *
 * A dev build is any build that finds the gitignored OtaPublicKey.local.h
 * (created by `utils/keygen.py --dev`).
 */
#if __has_include("../../OtaPublicKey.local.h")
#define FIRMWARE_DEV_BUILD 1
#else
#define FIRMWARE_DEV_BUILD 0
#endif

// Release builds get the commit hash from CI's build_opt.h; local builds show "dev"
#ifndef FIRMWARE_COMMIT
#define FIRMWARE_COMMIT "dev"
#endif

#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__
