#pragma once
#include "configuration.h"

#include "meshtastic/powermon.pb.h"

#ifndef MESHTASTIC_EXCLUDE_POWERMON
#define USE_POWERMON // FIXME turn this only for certain builds
#endif

/**
 * The singleton class for monitoring power consumption of device
 * subsystems/modes.
 *
 * For more information see the PowerMon docs.
 */
class PowerMon
{
    uint64_t states = 0UL;

  public:
    PowerMon() {}

    // Mark entry/exit of a power consuming state
    void setState(_meshtastic_PowerMon_State state, const char *reason = "");
    void clearState(_meshtastic_PowerMon_State state, const char *reason = "");

  private:
    // Emit the coded log message
    void emitLog(const char *reason);
};

extern PowerMon *powerMon;

void powerMonInit();