#include "Restart.h"

#include <atomic>

#include "Log.h"

namespace
{
std::atomic<bool> pending{false};
std::atomic<uint32_t> restartAt{0};
} // namespace

void Restart::schedule(uint32_t delayMs)
{
  restartAt = millis() + delayMs;
  pending = true;
}

void Restart::loop()
{
  if (pending && (int32_t)(millis() - restartAt) >= 0)
  {
    LOG("sys", "Restarting");
    ESP.restart();
  }
}
