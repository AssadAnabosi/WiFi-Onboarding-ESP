#pragma once

#include <Arduino.h>

/**
 * @brief Restarts the device from loop(), once the HTTP response that asked for it has been sent.
 */
namespace Restart
{
/**
 * @brief Restarts the device in `delayMs`. A later call replaces an earlier one. Safe from any task.
 */
void schedule(uint32_t delayMs);

/**
 * @brief Restarts once the scheduled time has come. Call from loop().
 */
void loop();
} // namespace Restart
