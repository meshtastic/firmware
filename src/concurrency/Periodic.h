#pragma once

#include <functional>
#include <utility>

#include "concurrency/OSThread.h"

namespace concurrency
{

/**
 * @brief Periodically invoke a callback.
 *        Supports both legacy function pointers and modern callables.
 */
class Periodic : public OSThread
{
  public:
    // callback returns the period for the next callback invocation (or 0 if we should no longer be called)
    Periodic(const char *name, int32_t (*cb)()) : OSThread(name), callback(cb) {}
    Periodic(const char *name, std::function<int32_t()> cb) : OSThread(name), callback(std::move(cb)) {}

  protected:
    int32_t runOnce() override { return callback ? callback() : 0; }

  private:
    std::function<int32_t()> callback;
};

} // namespace concurrency
