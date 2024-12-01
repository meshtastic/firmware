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

    friend class PowerStressModule;

    /**
     * If stress testing we always want all events logged
     */
    bool force_enabled = false;

  public:
    PowerMon() {}

    // Mark entry/exit of a power consuming state
    void setState(_meshtastic_PowerMon_State state, const char *reason = "");
    void clearState(_meshtastic_PowerMon_State state, const char *reason = "");

  private:
    // Emit the coded log message
    void emitLog(const char *reason);

    // Use the 'live' config flag to figure out if we should be showing this message
    bool is_power_enabled(uint64_t m);
};

extern PowerMon *powerMon;

void powerMonInit();