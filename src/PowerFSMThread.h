#include "Default.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "main.h"
#include "power.h"

namespace concurrency
{
/// Wrapper to convert our powerFSM stuff into a 'thread'
class PowerFSMThread : public OSThread
{
  public:
    // callback returns the period for the next callback invocation (or 0 if we should no longer be called)
    PowerFSMThread() : OSThread("PowerFSM") {}

  protected:
    int32_t runOnce() override
    {
#if !EXCLUDE_POWER_FSM
        powerFSM.run_machine();

        /// If we are in power state we force the CPU to wake every 10ms to check for serial characters (we don't yet wake
        /// cpu for serial rx - FIXME)
        const State *state = powerFSM.getState();
        canSleep = (state != &statePOWER) && (state != &stateSERIAL);

        if (powerStatus->getHasUSB()) {
            timeLastPowered = millis();
        } else if (config.power.on_battery_shutdown_after_secs > 0 && config.power.on_battery_shutdown_after_secs != UINT32_MAX &&
                   millis() > (timeLastPowered +
                               Default::getConfiguredOrDefaultMs(
                                   config.power.on_battery_shutdown_after_secs))) { // shutdown after 30 minutes unpowered
            powerFSM.trigger(EVENT_SHUTDOWN);
        }

        return 100;
#else
        return INT32_MAX;
#endif
    }
};

} // namespace concurrency