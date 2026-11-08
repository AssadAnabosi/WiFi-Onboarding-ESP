#pragma once

#include <HTTPClient.h>
#include <NetworkClientSecure.h>

/**
 * @brief One HTTP(S) GET that follows redirects (GitHub downloads redirect to another host).
 *
 * HTTPS uses the core's built-in root certificate bundle. No NTP: certificate dates are not checked.
 */
class HttpGet
{
public:
  ~HttpGet() { _http.end(); }

  /**
   * @brief Sends the request.
   * @return The HTTP status, or a negative HTTPC_ERROR_* code.
   */
  int open(const String &url);

  HTTPClient &http() { return _http; }

  /**
   * @brief Release builds only fetch over HTTPS; dev builds also allow plain HTTP.
   */
  static bool isAllowedUrl(const String &url);

  /**
   * @brief The device must be on a Wi-Fi network, and HTTPS needs enough free memory.
   * @return False with `error` and `message` set when `url` can't be fetched now.
   */
  static bool canFetch(const String &url, const char *&error, String &message);

private:
  NetworkClientSecure _secure;
  NetworkClient _plain;
  HTTPClient _http;
};
