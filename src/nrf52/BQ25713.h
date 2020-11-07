#pragma once

#include <Arduino.h>

/**
 * Driver class to control/monitor BQ25713 charge controller
 */
class BQ25713 {
    static const uint8_t devAddr;

public:

    /// Return true for success
    bool setup();

private:
    uint16_t readReg(uint8_t reg);

    /// Return true for success
    bool writeReg(uint8_t reg, uint16_t v);
};

