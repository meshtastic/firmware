#pragma once

#include "WorkerThread.h"

namespace concurrency {

/**
 * @brief A worker thread that waits on a freertos notification
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

} // namespace concurrency
