#pragma once

#include "freertosinc.h"
#include "esp_task_wdt.h"

namespace concurrency {

/**
 * @brief Base threading
 */
class Thread
{
  protected:
    TaskHandle_t taskHandle = NULL;

    /**
     * set this to true to ask thread to cleanly exit asap
     */
    volatile bool wantExit = false;

  public:
    void start(const char *name, size_t stackSize = 1024, uint32_t priority = tskIDLE_PRIORITY);

    virtual ~Thread() { vTaskDelete(taskHandle); }

    uint32_t getStackHighwaterMark() { return uxTaskGetStackHighWaterMark(taskHandle); }

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
    void serviceWatchdog() { esp_task_wdt_reset(); }
    void startWatchdog()
    {
        auto r = esp_task_wdt_add(taskHandle);
        assert(r == ESP_OK);
    }
    void stopWatchdog()
    {
        auto r = esp_task_wdt_delete(taskHandle);
        assert(r == ESP_OK);
    }

  private:
    static void callRun(void *_this);
};


} // namespace concurrency
