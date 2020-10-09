#pragma once

#include "OSThread.h"

namespace concurrency
{

/**
 * @brief This wraps threading (FreeRTOS for now) with a blocking API intended for efficiently converting
 *        old-school arduino loop() code. Use as a mixin base class for the classes you want to convert.
 *
 * @link https://www.freertos.org/RTOS_Task_Notification_As_Mailbox.html
 */
class WorkerThread : public OSThread
{
  protected:
    /**
     * Return true if this thread is ready to run - either waiting ona queue/mutex or a "task notification"
     */
    virtual bool shouldRun() = 0;

    virtual void loop() = 0;
};

} // namespace concurrency
