#include "RequestGuard.h"

#include <WiFi.h>

#include "../../ProjectConfig.h"

namespace
{
// Exact names only: the access point and network IPs, the setup name and the hostname
bool isOwnHost(String host)
{
  host.toLowerCase();
  if (host.isEmpty())
    return false;

  if ((WiFi.getMode() & WIFI_AP) && host == WiFi.softAPIP().toString())
    return true;
  if (WiFi.status() == WL_CONNECTED && host == WiFi.localIP().toString())
    return true;
  if (host == MDNS_SETUP_NAME ".local")
    return true;

  String hostname = WiFi.getHostname();
  hostname.toLowerCase();
  return !hostname.isEmpty() && (host == hostname || host == hostname + ".local");
}

// "http://<own host>[:80]"
bool isOwnOrigin(String origin)
{
  origin.toLowerCase();
  if (!origin.startsWith("http://"))
    return false;
  String host = origin.substring(7);
  int colon = host.indexOf(':');
  if (colon >= 0)
  {
    if (host.substring(colon + 1) != "80")
      return false;
    host = host.substring(0, colon);
  }
  return isOwnHost(host);
}
} // namespace

bool RequestGuard::isTrustedPost(AsyncWebServerRequest *request, String &reason)
{
  // Bodiless POSTs must still declare JSON
  const String &type = request->contentType();
  if (!type.equalsIgnoreCase("application/json") && !type.equalsIgnoreCase("application/octet-stream"))
  {
    reason = "Unsupported content type";
    return false;
  }

  String host = request->host();
  int colon = host.indexOf(':');
  if (colon >= 0)
    host = host.substring(0, colon);
  if (!isOwnHost(host))
  {
    reason = "Request host not allowed";
    return false;
  }

  // Tools such as curl send no Origin; browsers always do on POST
  const AsyncWebHeader *origin = request->getHeader("Origin");
  if (origin && !isOwnOrigin(origin->value()))
  {
    reason = "Request origin not allowed";
    return false;
  }
  return true;
}
