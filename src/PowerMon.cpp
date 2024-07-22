#include "PowerMon.h"
#include "NodeDB.h"

// Use the 'live' config flag to figure out if we should be showing this message
static bool is_power_enabled(uint64_t m)
{
    return (m & config.power.powermon_enables) ? true : false;
}

void PowerMon::setState(_meshtastic_PowerMon_State state, const char *reason)
{
#ifdef USE_POWERMON
    auto oldstates = states;
    states |= state;
    if (oldstates != states && is_power_enabled(state)) {
        emitLog(reason);
    }
#endif
}

void PowerMon::clearState(_meshtastic_PowerMon_State state, const char *reason)
{
#ifdef USE_POWERMON
    auto oldstates = states;
    states &= ~state;
    if (oldstates != states && is_power_enabled(state)) {
        emitLog(reason);
    }
#endif
}

void PowerMon::emitLog(const char *reason)
{
#ifdef USE_POWERMON
    // The nrf52 printf doesn't understand 64 bit ints, so if we ever reach that point this function will need to change.
    LOG_INFO("S:PM:0x%08lx,%s\n", (uint32_t)states, reason);
#endif
}

PowerMon *powerMon;

void powerMonInit()
{
    powerMon = new PowerMon();
}