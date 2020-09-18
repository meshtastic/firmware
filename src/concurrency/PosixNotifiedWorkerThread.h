#pragma once

#include "BaseNotifiedWorkerThread.h"

namespace concurrency {

/**
 * @brief A worker thread that waits on a freertos notification
 */
class PosixNotifiedWorkerThread : public BaseNotifiedWorkerThread
{
  public:
    /**
     * Notify this thread so it can run
     */
    void notify(uint32_t v = 0, eNotifyAction action = eNoAction);

  protected:

    /**
     * A method that should block execution - either waiting ona queue/mutex or a "task notification"
     */
    virtual void block();
};

} // namespace concurrency
