#pragma once

/**
 * @brief Everything a fork changes to make the project its own.
 * See docs/make-it-yours.md.
 */

// Identifies this project's update files; files from another project are rejected
#define PROJECT_SLUG "wifi-onboarding"

// GitHub repository whose releases the device pulls updates from
#define OTA_GITHUB_OWNER "AssadAnabosi"
#define OTA_GITHUB_REPO "WiFi-Onboarding-ESP"

// Build target used when build_opt.h does not set one
#ifndef OTA_TARGET_ID
#define OTA_TARGET_ID "esp32-4mb"
#endif
#ifndef OTA_TARGET_HEADER
#define OTA_TARGET_HEADER "targets/esp32-4mb.h"
#endif

// Update checks: first check after boot, then every interval plus a random offset
#define OTA_CHECK_FIRST_DELAY_MS (30UL * 1000UL)
#define OTA_CHECK_INTERVAL_MS (24UL * 60UL * 60UL * 1000UL)
#define OTA_CHECK_JITTER_MS (60UL * 60UL * 1000UL)

// Device defaults, used until the user changes them
#define DEFAULT_AP_SSID "ESP-Access-Point"
#define DEFAULT_AP_PASSWORD "12345678"
#define DEFAULT_AP_CHANNEL 6
#define DEFAULT_AP_HIDDEN false
#define DEFAULT_AP_ENABLED true
#define DEFAULT_HOSTNAME "ESP-IoT"

// Fixed setup address: <name>.local
#define MDNS_SETUP_NAME "config"

#include OTA_TARGET_HEADER

#include <soc/soc_caps.h>
#if !SOC_WIFI_SUPPORTED
#error "This chip has no Wi-Fi. Wi-Fi onboarding and OTA updates need Wi-Fi, so it is not supported (e.g. ESP32-H2, ESP32-P4)."
#endif
