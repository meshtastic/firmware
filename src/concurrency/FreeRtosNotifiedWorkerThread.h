#pragma once

#include "BaseNotifiedWorkerThread.h"

#ifdef HAS_FREE_RTOS

namespace concurrency {

/**
 * @brief A worker thread that waits on a freertos notification
 */
class FreeRtosNotifiedWorkerThread : public BaseNotifiedWorkerThread
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
     * A method that should block execution - either waiting ona queue/mutex or a "task notification"
     */
    virtual void block();
};

} // namespace concurrency

#endif