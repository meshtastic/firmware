#pragma once

#include <stdint.h>

constexpr bool matchesIna219ConfigurationSignature(uint16_t configuration)
{
    // Range and gain are configurable; retain the reset ADC/mode signature at these shared addresses.
    return (configuration & ~uint16_t{0x3800}) == 0x019f;
}
