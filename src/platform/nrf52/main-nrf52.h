#pragma once

#ifdef ARCH_NRF52

// Internal to the nRF52 platform: declarations shared between main-nrf52.cpp and the
// other files under src/platform/nrf52. Nothing outside this directory should include it.

// DC/DC buck converter control. nrf52EnableDCDC() runs from powerHAL_platformInit(),
// which is before consoleInit(), so the outcome is latched and reported later by
// nrf52LogDCDCStatus(). nrf52ReassertDCDC() re-applies the mode once the SoftDevice
// owns the POWER peripheral. All three are no-ops unless the board sets
// NRF52_USE_DCDC_REG0 and/or NRF52_USE_DCDC_REG1.
void nrf52EnableDCDC(), nrf52LogDCDCStatus(), nrf52ReassertDCDC();

#endif
