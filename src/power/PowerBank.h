#pragma once

#include "configuration.h"

#if HAS_POWERBANK
#include <stdint.h>

// Battery and input state reported by a power-bank controller, in the terms Power needs. On a board
// with EXT_CHRG_DETECT, Power reads charging and external power from that pin instead.
struct PowerBankStatus {
    uint16_t batteryMv = 0;
    int8_t batteryPercent = -1;
    bool charging = false;
    bool inputPresent = false;
};

// A power-bank controller that reports its own battery and input state.
class PowerBank
{
  public:
    virtual ~PowerBank() = default;

    // False while no fresh status is available.
    virtual bool getStatus(PowerBankStatus &out) = 0;
};

// Starts the board's power-bank controller; provided by the controller's driver.
PowerBank *powerBankSetup();
#endif
