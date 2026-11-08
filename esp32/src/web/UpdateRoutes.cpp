// /api/update (manual upload), /api/update/status, /api/update/check, /api/update/install, /api/update/rollback

#include "WebServerManager.h"

#include "../ota/UpdateManager.h"
#include "../system/Restart.h"
#include "RequestGuard.h"

namespace
{
constexpr uint32_t UPDATE_RESTART_DELAY_MS = 1000;

/**
 * @brief Per-request state of a manual upload. Allocated with calloc: the server frees it with the request.
 */
struct UploadContext
{
  bool owner; ///< This request holds the update lock and is writing to flash
  int code;
  char error[24];
  char message[160];
};

void reject(UploadContext *ctx, const UpdateReply &reply)
{
  ctx->owner = false;
  ctx->code = reply.status;
  strlcpy(ctx->error, reply.error.c_str(), sizeof(ctx->error));
  strlcpy(ctx->message, reply.message.c_str(), sizeof(ctx->message));
}
} // namespace

void WebServerManager::registerUpdateRoutes()
{
  // Longer paths first: server.on("/api/update") also matches "/api/update/..."
  _server.on("/api/update/status", HTTP_GET, [this](AsyncWebServerRequest *request)
             { handleUpdateStatus(request); });

  _server.on("/api/update/check", HTTP_POST, [this](AsyncWebServerRequest *request)
             { sendMessage(request, _updates.requestCheck()); });

  _server.on("/api/update/install", HTTP_POST, [this](AsyncWebServerRequest *request)
             { sendMessage(request, _updates.requestInstall()); });

  _server.on("/api/update/rollback", HTTP_POST, [this](AsyncWebServerRequest *request)
             { handleRollback(request); });

  _server.on(
      "/api/update", HTTP_POST,
      [this](AsyncWebServerRequest *request)
      { handleUploadDone(request); },
      nullptr,
      [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
      { handleUploadBody(request, data, len, index, total); });
}

void WebServerManager::handleUpdateStatus(AsyncWebServerRequest *request)
{
  JsonDocument doc;
  _updates.fillStatus(doc.to<JsonObject>());
  sendJson(request, doc);
}

void WebServerManager::handleRollback(AsyncWebServerRequest *request)
{
  UpdateReply reply = _updates.requestRollback();
  sendMessage(request, reply);
  if (reply.status == 200)
    Restart::schedule(UPDATE_RESTART_DELAY_MS);
}

void WebServerManager::handleUploadBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  if (index == 0)
  {
    UploadContext *ctx = (UploadContext *)calloc(1, sizeof(UploadContext));
    if (!ctx)
      return;
    request->_tempObject = ctx;

    // The middleware only runs once the body has arrived: check before writing to flash
    if (!request->contentType().equalsIgnoreCase("application/octet-stream"))
    {
      reject(ctx, {415, "invalid_file", "Send the update as application/octet-stream"});
      return;
    }
    String reason;
    if (!RequestGuard::isTrustedPost(request, reason))
    {
      reject(ctx, {403, "forbidden", reason});
      return;
    }

    UpdateFile::Options options;
    options.allowDowngrade = request->hasParam("allow_downgrade");
    options.allowTargetChange = request->hasParam("allow_target_change");
    UpdateReply reply = _updates.uploadBegin(total, data, len, options);
    if (reply.status != 200)
    {
      reject(ctx, reply);
      return;
    }
    ctx->owner = true;
    request->onDisconnect([this]()
                          { _updates.uploadCancel(); });
  }

  UploadContext *ctx = (UploadContext *)request->_tempObject;
  if (!ctx || !ctx->owner)
    return;

  if (!_updates.uploadWrite(data, len, index))
  {
    reject(ctx, _updates.lastFailure());
    return;
  }
  if (index + len == total)
  {
    if (_updates.uploadFinish())
      ctx->code = 200;
    else
      reject(ctx, _updates.lastFailure());
  }
}

void WebServerManager::handleUploadDone(AsyncWebServerRequest *request)
{
  UploadContext *ctx = (UploadContext *)request->_tempObject;
  if (!ctx)
  {
    sendMessage(request, "Send the update file as the request body", 411);
    return;
  }
  if (ctx->code == 200)
  {
    sendMessage(request, "Update installed, rebooting");
    Restart::schedule(UPDATE_RESTART_DELAY_MS);
    return;
  }
  sendFailure(request, {ctx->code ? ctx->code : 400, ctx->error, ctx->message});
}
