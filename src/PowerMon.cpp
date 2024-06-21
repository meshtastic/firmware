#include "PowerMon.h"
#include "configuration.h"

void PowerMon::setState(_meshtastic_PowerMon_State state, const char *reason)
{
#ifdef USE_POWERMON
    auto oldstates = states;
    states |= state;
    if (oldstates != states) {
        emitLog(reason);
    }
#endif
}

void PowerMon::clearState(_meshtastic_PowerMon_State state, const char *reason)
{
#ifdef USE_POWERMON
    auto oldstates = states;
    states &= ~state;
    if (oldstates != states) {
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