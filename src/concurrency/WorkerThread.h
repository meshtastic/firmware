#pragma once

#include "Thread.h"

namespace concurrency {

/**
 * @brief This wraps threading (FreeRTOS for now) with a blocking API intended for efficiently converting 
 *        old-school arduino loop() code. Use as a mixin base class for the classes you want to convert.
 *
 * @link https://www.freertos.org/RTOS_Task_Notification_As_Mailbox.html
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

} // namespace concurrency
