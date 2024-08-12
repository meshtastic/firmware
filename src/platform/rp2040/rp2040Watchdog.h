#pragma once
#include "configuration.h"

class Rp2040Watchdog : public concurrency::OSThread
{
  public:
    explicit Rp2040Watchdog() : OSThread("Rp2040Watchdog")
    {
        LOG_TRACE("Rp2040Watchdog::Rp2040Watchdog(): Initializing\n");
        watchdogIsRunning = false;
        statusOutputCount = 0;

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
            uint32_t timeout = currentTick - lastResetTick;

            if (currentTick < lastTick) {
                rollover++;
            }
            uint64_t uptime = (rollover * 0xFFFFFFFF) + currentTick;
            lastTick = currentTick;

            // Dump trace output aprox. each minute, just to allow some sort of feeling with the watchdog
            // (We do not handle 49 days rollover, that's not important, we just want to know if
            // the device has been up for days, and not with hours between restarts)
            if (statusOutputCount++ > 14) {
                LOG_TRACE("Rp2040Watchdog->runOnce(): watchdog running (timeout = %u seconds, uptime = %lu minutes)\n", timeout,
                          uptime / (60 * 1000));
                statusOutputCount = 0;
            }

            if (timeout < 90 * 1000) { // 90 seconds, same as esp32 watchdog
                watchdog_update();
            } else {
                LOG_ERROR("Rp2040Watchdog->runOnce(): watchdog time since last update has exceeded timeout (timeout = %u "
                          "seconds, uptime = %lu minutes)\n",
                          timeout, uptime / (60 * 1000));
                LOG_ERROR("Rp2040Watchdog->runOnce(): WAITING FOR REBOOT\n");
            }
        }

        // Time until this thread runs again
        return 4 * 1000; // 4 seconds
    }

  private:
    uint32_t lastResetTick;
    uint32_t lastTick;
    uint32_t rollover;

    bool watchdogIsRunning;

    int statusOutputCount;
};

extern Rp2040Watchdog *rp2040Watchdog;