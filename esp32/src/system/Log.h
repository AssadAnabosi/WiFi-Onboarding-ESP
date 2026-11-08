#pragma once

#include <Arduino.h>

/**
 * @brief Writes one line to the serial log, prefixed with the module's tag.
 *
 * Example: LOG("wifi", "Connected to %s", ssid.c_str()) prints "[wifi] Connected to Home".
 * Never log passwords.
 */
#define LOG(tag, format, ...) Serial.printf("[" tag "] " format "\n", ##__VA_ARGS__)
