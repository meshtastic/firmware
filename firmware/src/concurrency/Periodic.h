#pragma once

#include "concurrency/OSThread.h"

namespace concurrency
{

/**
 * @brief Periodically invoke a callback. This just provides C-style callback conventions
 *        rather than a virtual function - FIXME, remove?
 */
class Periodic : public OSThread
{
    int32_t (*callback)();

  public:
    // callback returns the period for the next callback invocation (or 0 if we should no longer be called)
    Periodic(const char *name, int32_t (*_callback)()) : OSThread(name), callback(_callback) {}

  protected:
    int32_t runOnce() override { return callback(); }
};

} // namespace concurrency
