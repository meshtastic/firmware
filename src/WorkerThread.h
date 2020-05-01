#include <Arduino.h>

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

  protected:
    /**
     * The method that will be called when start is called.
     */
    virtual void doRun() = 0;

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
     */
    void notifyFromISR(BaseType_t *highPriWoken, uint32_t v = 0, eNotifyAction action = eNoAction);

  protected:
    /**
     * The notification that was most recently used to wake the thread.  Read from loop()
     */
    uint32_t notification = 0;

    /**
     * A method that should block execution - either waiting ona queue/mutex or a "task notification"
     */
    virtual void block();
};