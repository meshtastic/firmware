#pragma once

#include "BaseThread.h"
#include "freertosinc.h"

#ifdef HAS_FREE_RTOS

namespace concurrency
{

/**
 * @brief Base threading
 */
class FreeRtosThread : public BaseThread
{
  protected:
    TaskHandle_t taskHandle = NULL;

  public:
    void start(const char *name, size_t stackSize = 1024, uint32_t priority = tskIDLE_PRIORITY);

    virtual ~FreeRtosThread() { vTaskDelete(taskHandle); }

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
    void serviceWatchdog();
    void startWatchdog();
    void stopWatchdog();
};

} // namespace concurrency

#endif