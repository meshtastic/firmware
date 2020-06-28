#include "esp_task_wdt.h"
#include "freertosinc.h"
#include <Arduino.h>

#ifdef HAS_FREE_RTOS

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

/**
 * This wraps threading (FreeRTOS for now) with a blocking API intended for efficiently converting onlyschool arduino loop() code.
 *
 * Use as a mixin base class for the classes you want to convert.
 *
 * https://www.freertos.org/RTOS_Task_Notification_As_Mailbox.html
 */
class WorkerThread : public Thread
{
  protected:
    /**
     * A method that should block execution - either waiting ona queue/mutex or a "task notification"
     */
    virtual void block() = 0;

    virtual void loop() = 0;

    /**
     * The method that will be called when start is called.
     */
    virtual void doRun();
};

/**
 * A worker thread that waits on a freertos notification
 */
class NotifiedWorkerThread : public WorkerThread
{
  public:
    /**
     * Notify this thread so it can run
     */
    void notify(uint32_t v = 0, eNotifyAction action = eNoAction);

    /**
     * Notify from an ISR
     *
     * This must be inline or IRAM_ATTR on ESP32
     */
    inline void notifyFromISR(BaseType_t *highPriWoken, uint32_t v = 0, eNotifyAction action = eNoAction)
    {
        xTaskNotifyFromISR(taskHandle, v, action, highPriWoken);
    }

  protected:
    /**
     * The notification that was most recently used to wake the thread.  Read from loop()
     */
    uint32_t notification = 0;

    /**
     * What notification bits should be cleared just after we read and return them in notification?
     *
     * Defaults to clear all of them.
     */
    uint32_t clearOnRead = UINT32_MAX;

    /**
     * A method that should block execution - either waiting ona queue/mutex or a "task notification"
     */
    virtual void block();
};

#endif