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
    virtual void notify(uint32_t v = 0, eNotifyAction action = eNoAction) = 0;

    /**
     * Notify from an ISR
     *
     * This must be inline or IRAM_ATTR on ESP32
     */
    virtual void notifyFromISR(BaseType_t *highPriWoken, uint32_t v = 0, eNotifyAction action = eNoAction) { notify(v, action); }
    
  protected:
    /**
     * The notification that was most recently used to wake the thread.  Read from loop()
     */
    uint32_t notification = 0;

    /**
     * A method that should block execution - either waiting ona queue/mutex or a "task notification"
     */
    virtual void block() = 0;
};

} // namespace concurrency
