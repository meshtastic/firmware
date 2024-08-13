#pragma once
#include "configuration.h"

class Rp2040Watchdog : public concurrency::OSThread
{
  public:
    explicit Rp2040Watchdog() : OSThread("Rp2040Watchdog")
    {
        LOG_TRACE("Rp2040Watchdog::Rp2040Watchdog(): Initializing\n");
        watchdogIsRunning = false;

        // Todo: We need a port of the 'Preferences' type to rp2040 so that
        //       we can keep track of reboots
    }

    void reset() { lastResetTick = to_ms_since_boot(get_absolute_time()); }

  protected:
    int32_t runOnce() override
    {
        // If we are long enough into boot/initialization, start the hardware watchdog,
        // otherwise check if timer is below timeout and update the watchdog.
        uint32_t currentTick = to_ms_since_boot(get_absolute_time());
        if (!watchdogIsRunning) {

            LOG_TRACE("Rp2040Watchdog->runOnce(): watchdog not running (uptime = %u seconds)\n", currentTick / 1000);

            if (currentTick > 30 * 1000) { // 30 seconds into boot
                LOG_INFO("Rp2040Watchdog->runOnce(): starting watchdog (currentTick = %u seconds)\n", currentTick / 1000);
                watchdog_enable(0x7fffff, true);
                watchdogIsRunning = true;
                lastResetTick = currentTick;
            }
        } else {

            // Check for rollover, each 49 days. If so, then assume all is good
            if (currentTick < lastResetTick) {

                LOG_TRACE("Rp2040Watchdog->runOnce(): rollover detected (lastResetTick = %lu minutes)\n",
                          lastResetTick / (60 * 1000));
                lastResetTick = currentTick;
            }

            // Time since last reset
            uint32_t timeout = currentTick - lastResetTick;

            // Update watchdog if timeout is below 90 seconds, same as esp32 watchdog
            if (timeout < 90 * 1000) {
                watchdog_update();
            } else {
                LOG_ERROR("Rp2040Watchdog->runOnce(): watchdog time since last update has exceeded timeout (timeout = %u "
                          "seconds, uptime = %lu minutes)\n",
                          timeout, currentTick / (60 * 1000));
                LOG_ERROR("Rp2040Watchdog->runOnce(): WAITING FOR REBOOT\n");
            }
        }

        // Time until this thread runs again
        return 4 * 1000; // 4 seconds
    }

  private:
    uint32_t lastResetTick;
    bool watchdogIsRunning;
};

extern Rp2040Watchdog *rp2040Watchdog;