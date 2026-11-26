#pragma once

#include <stdint.h>

constexpr bool matchesDs248xResetStatus(uint16_t status)
{
    // The scan's 0xF0 write is a DS248X Device Reset, after which the status register has RST (bit 4) set.
    // The same test the Adafruit driver applies, so the scan claims nothing the driver would reject.
    return status != 0xFF && (status & 0x10) != 0;
}
