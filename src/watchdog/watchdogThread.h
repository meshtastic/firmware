#pragma once

#include "concurrency/OSThread.h"
#include <stdint.h>

#ifdef HAS_HARDWARE_WATCHDOG
class WatchdogThread : private concurrency::OSThread
{
  public:
    WatchdogThread();
    void feedDog(void);
    virtual bool setup();
    virtual int32_t runOnce() override;
};

extern WatchdogThread *watchdogThread;
#endif
