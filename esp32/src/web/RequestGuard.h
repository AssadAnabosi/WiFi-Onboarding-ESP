#pragma once

#include <ESPAsyncWebServer.h>

/**
 * @brief Cross-site request protection for POST routes.
 *
 * Accepts only JSON or raw binary bodies (browsers must ask before sending those to another
 * site, and the device never grants it: no CORS headers), a Host that is one of the device's
 * own names, and (when present) an Origin pointing at the device's own pages.
 */
namespace RequestGuard
{
/**
 * @return True if the request may be handled; otherwise false with `reason` set.
 */
bool isTrustedPost(AsyncWebServerRequest *request, String &reason);
} // namespace RequestGuard
