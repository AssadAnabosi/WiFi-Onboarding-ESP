#include "WebServerManager.h"

#include "../../WebAssets.h"
#include "../ota/FirmwareSlots.h"
#include "../ota/UpdateManager.h"
#include "../system/Log.h"
#include "RequestGuard.h"

namespace
{
// Pages and unversioned requests: ask the device before every use (it answers 304 while unchanged)
constexpr const char *CACHE_REVALIDATE = "no-cache";
// CSS/JS requested with their current "?v=<content hash>": new content gets a new URL
constexpr const char *CACHE_FOREVER = "public, max-age=31536000, immutable";
} // namespace

WebServerManager::WebServerManager(WiFiManager &wifi, UpdateManager &updates, FirmwareSlots &slots)
    : _server(80), _wifi(wifi), _updates(updates), _slots(slots)
{
}

void WebServerManager::begin()
{
  // Browsers must use each response's declared type, never guess one from its bytes
  DefaultHeaders::Instance().addHeader("X-Content-Type-Options", "nosniff");
  _server.addMiddleware(guardPost);

  // Pages and static files, embedded in the firmware
  for (size_t i = 0; i < WEB_ASSETS_COUNT; i++)
  {
    const WebAsset *asset = &WEB_ASSETS[i];
    _server.on(asset->url, HTTP_GET, [asset](AsyncWebServerRequest *request)
               { serveAsset(request, asset); });
  }

  registerSystemRoutes();
  registerWifiRoutes();
  registerUpdateRoutes();

  _server.onNotFound([](AsyncWebServerRequest *request)
                     { sendMessage(request, request->url() + " Not Found", 404); });

  _server.begin();
  LOG("web", "Web server started");
}

void WebServerManager::guardPost(AsyncWebServerRequest *request, ArMiddlewareNext next)
{
  String reason;
  if (request->method() == HTTP_POST && !RequestGuard::isTrustedPost(request, reason))
  {
    sendMessage(request, reason, 403);
    return;
  }
  next();
}

void WebServerManager::sendJson(AsyncWebServerRequest *request, const JsonDocument &doc, int code)
{
  String response;
  serializeJson(doc, response);
  request->send(code, "application/json", response);
}

void WebServerManager::sendMessage(AsyncWebServerRequest *request, const String &message, int code)
{
  JsonDocument doc;
  doc["success"] = code >= 200 && code < 300;
  doc["message"] = message;
  LOG("web", "%d %s", code, message.c_str());
  sendJson(request, doc, code);
}

void WebServerManager::sendMessage(AsyncWebServerRequest *request, const UpdateReply &reply)
{
  sendMessage(request, reply.message, reply.status);
}

void WebServerManager::sendFailure(AsyncWebServerRequest *request, const UpdateReply &reply)
{
  JsonDocument doc;
  doc["success"] = false;
  doc["error"] = reply.error;
  doc["message"] = reply.message;
  LOG("web", "%d %s", reply.status, reply.message.c_str());
  sendJson(request, doc, reply.status);
}

void WebServerManager::serveAsset(AsyncWebServerRequest *request, const WebAsset *asset)
{
  // Pages link their CSS/JS as "file?v=<content hash>" (build_assets.py). A request with
  // the current hash can be kept forever: new content gets a new URL. Everything else,
  // pages included, is revalidated on every visit, so new firmware never runs with stale files.
  // The server's built-in 304 handling only covers filesystem files, so compare here.
  bool versioned = false;
  if (request->hasParam("v"))
  {
    String expected = String("\"") + request->getParam("v")->value() + "\"";
    versioned = expected == asset->etag;
  }
  const AsyncWebHeader *ifNoneMatch = request->getHeader("If-None-Match");
  bool notModified = ifNoneMatch && ifNoneMatch->value().indexOf(asset->etag) >= 0;
  AsyncWebServerResponse *response = notModified ? request->beginResponse(304)
                                                 : request->beginResponse(200, asset->mime, asset->data, asset->length);
  if (!notModified)
    response->addHeader("Content-Encoding", "gzip");
  response->addHeader("ETag", asset->etag);
  response->addHeader("Cache-Control", versioned ? CACHE_FOREVER : CACHE_REVALIDATE);
  request->send(response);
}

bool WebServerManager::refuseWhileUnconfirmed(AsyncWebServerRequest *request)
{
  if (!_slots.isUnconfirmed())
    return false;
  sendMessage(request, _slots.finishingMessage(), 409);
  return true;
}
