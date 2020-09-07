#pragma once

#include <cstdlib>
#include <stdint.h>

#include "freertosinc.h"

namespace concurrency
{

/**
 * @brief Base threading
 */
class BaseThread
{
  protected:
    /**
     * set this to true to ask thread to cleanly exit asap
     */
    volatile bool wantExit = false;

  public:
    virtual void start(const char *name, size_t stackSize = 1024, uint32_t priority = tskIDLE_PRIORITY) = 0;

    virtual ~BaseThread() {}

    // uint32_t getStackHighwaterMark() { return uxTaskGetStackHighWaterMark(taskHandle); }

  protected:
    /**
     * The method that will be called when start is called.
     */
    virtual void doRun() = 0;

    /**
     * All thread run methods must periodically call serviceWatchdog, or the system will declare them hung and panic.
     *
     * this only applies after startWatchdog() has been called.  If you need to sleep for a long time call stopWatchdog()
     */
    virtual void serviceWatchdog() {}
    virtual void startWatchdog() {}
    virtual void stopWatchdog() {}

    static void callRun(void *_this);
};

} // namespace concurrency
