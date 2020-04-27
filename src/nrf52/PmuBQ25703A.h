#pragma once

#include <Lorro_BQ25703A.h>

class PmuBQ25703A : private Lorro_BQ25703A
{
    Lorro_BQ25703A::Regt regs;

  public:
    /**
     * Configure the PMU for our board
     */
    void init();

    // Methods to have a common API with AXP192
    bool isBatteryConnect() { return true; } // FIXME
    bool isVBUSPlug() { return true; }
    bool isChargeing() { return true; } // FIXME, intentional misspelling

    /// battery voltage in mV
    int getBattVoltage() { return 3200; }
};
