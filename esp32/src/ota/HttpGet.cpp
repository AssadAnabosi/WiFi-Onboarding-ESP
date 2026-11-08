#include "HttpGet.h"

#include <WiFi.h>

#include "../system/BuildInfo.h"

namespace
{
constexpr uint32_t HTTP_TIMEOUT_MS = 15000;
constexpr uint32_t HTTPS_MIN_FREE_HEAP = 60 * 1024;
} // namespace

int HttpGet::open(const String &url)
{
  NetworkClient *client = &_plain;
  if (url.startsWith("https://"))
  {
    _secure.useBuiltinCACertBundle();
    client = &_secure;
  }
  _http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  _http.setConnectTimeout(HTTP_TIMEOUT_MS);
  _http.setTimeout(HTTP_TIMEOUT_MS);
  _http.useHTTP10(true); // No chunked encoding: the body is read as a plain stream
  if (!_http.begin(*client, url))
    return HTTPC_ERROR_CONNECTION_REFUSED;
  return _http.GET();
}

bool HttpGet::isAllowedUrl(const String &url)
{
  return url.startsWith("https://") || (FIRMWARE_DEV_BUILD && url.startsWith("http://"));
}

bool HttpGet::canFetch(const String &url, const char *&error, String &message)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    error = "no_internet";
    message = "No internet: the device is not connected to a Wi-Fi network (access point only)";
    return false;
  }
  if (url.startsWith("https://") && ESP.getFreeHeap() < HTTPS_MIN_FREE_HEAP)
  {
    error = "not_enough_memory";
    message = "Not enough free memory for a secure download (" + String(ESP.getFreeHeap() / 1024) + " KB free, " +
              String(HTTPS_MIN_FREE_HEAP / 1024) + " KB needed)";
    return false;
  }
  return true;
}
