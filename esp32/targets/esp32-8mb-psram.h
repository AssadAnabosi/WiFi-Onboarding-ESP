#pragma once

/**
 * @brief Feature switches for the esp32-8mb-psram target.
 *
 * Classic ESP32 with 8 MB flash and PSRAM (WROVER, WROVER-E). The sketch.yaml profile turns
 * PSRAM on (PSRAM=enabled: BOARD_HAS_PSRAM), so the core finds it at boot, adds it to the heap
 * and /api/health reports it. A board without PSRAM still boots and reports 0.
 * Flash layout: esp32/targets/esp32-8mb-psram.partitions.csv (the same as esp32-8mb).
 */

// How the browser installer lists this target (utils/release.py install); utils/targets.py check requires both
#define TARGET_TITLE "8 MB flash + PSRAM"
#define TARGET_DESCRIPTION "Classic ESP32 with 8 MB flash and PSRAM: ESP32-WROVER, -WROVER-E (e.g. ESP32-WROVER-DevKitC). Uses the extra RAM."
