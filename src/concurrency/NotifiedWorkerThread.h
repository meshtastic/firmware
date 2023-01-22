#pragma once

#include "OSThread.h"

namespace concurrency
{

/**
 * @brief A worker thread that waits on a freertos notification
 */
class NotifiedWorkerThread : public OSThread
{
    /**
     * The notification that was most recently used to wake the thread.  Read from runOnce()
     */
    uint32_t notification = 0;

  public:
    NotifiedWorkerThread(const char *name) : OSThread(name) {}

    /**
     * Notify this thread so it can run
     */
    bool notify(uint32_t v, bool overwrite);

    /**
     * Notify from an ISR
     *
     * This must be inline or IRAM_ATTR on ESP32
     */
    bool notifyFromISR(BaseType_t *highPriWoken, uint32_t v, bool overwrite);

    /**
     * Schedule a notification to fire in delay msecs
     */
    bool notifyLater(uint32_t delay, uint32_t v, bool overwrite);

  protected:
    virtual void onNotify(uint32_t notification) = 0;

    /// just calls checkNotification()
    virtual int32_t runOnce() override;

    /// Sometimes we might want to check notifications independently of when our thread was getting woken up (i.e. if we are about
    /// to change radio transmit/receive modes we want to handle any pending interrupts first).  You can call this method and if
    /// any notifications are currently pending they will be handled immediately.
    void checkNotification();

  private:
    /**
     * Notify this thread so it can run
     */
    bool notifyCommon(uint32_t v, bool overwrite);
};

} // namespace concurrency
