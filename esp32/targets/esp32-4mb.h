#pragma once

/**
 * @brief Feature switches for the esp32-4mb target.
 *
 * Classic ESP32 with 4 MB flash (WROOM, WROVER, PICO-D4). PSRAM is not turned on.
 * Flash layout: esp32/targets/esp32-4mb.partitions.csv.
 */

// How the browser installer lists this target (utils/release.py install); utils/targets.py check requires both
#define TARGET_TITLE "4 MB flash"
#define TARGET_DESCRIPTION "Any classic ESP32 with 4 MB flash or more: WROOM, WROVER, PICO-D4. Also runs on 8 MB boards, using half the flash."
