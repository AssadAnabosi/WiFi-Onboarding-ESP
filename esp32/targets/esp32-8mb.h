#pragma once

/**
 * @brief Feature switches for the esp32-8mb target.
 *
 * Classic ESP32 with 8 MB flash (WROOM, WROVER). PSRAM is not turned on
 * (esp32-8mb-psram does).
 * Flash layout: esp32/targets/esp32-8mb.partitions.csv.
 */

// How the browser installer lists this target (utils/release.py install); utils/targets.py check requires both
#define TARGET_TITLE "8 MB flash"
#define TARGET_DESCRIPTION "Classic ESP32 with 8 MB flash or more, e.g. ESP32-WROOM-32E-N8. Leaves PSRAM off if the board has it."
