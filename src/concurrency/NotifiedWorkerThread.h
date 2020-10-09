#pragma once

#include "OSThread.h"

namespace concurrency
{

/**
 * @brief A worker thread that waits on a freertos notification
 */
class NotifiedWorkerThread : public OSThread
{
  public:
    NotifiedWorkerThread(const char *name) : OSThread(name) {}

    /**
     * Notify this thread so it can run
     */
    void notify(uint32_t v, bool overwrite);

    /**
     * Notify from an ISR
     *
     * This must be inline or IRAM_ATTR on ESP32
     */
    void notifyFromISR(BaseType_t *highPriWoken, uint32_t v, bool overwrite) { notify(v, overwrite); }

    /**
     * Schedule a notification to fire in delay msecs
     */
    void notifyLater(uint32_t delay, uint32_t v, bool overwrite);

  protected:
    virtual void onNotify(uint32_t notification) = 0;

    virtual uint32_t runOnce();

  private:
    /**
     * The notification that was most recently used to wake the thread.  Read from runOnce()
     */
    uint32_t notification = 0;
};

} // namespace concurrency
