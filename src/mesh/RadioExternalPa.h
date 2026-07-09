#pragma once

#include <stdint.h>

/**
 * Generic hooks for boards with an external power amplifier whose gain/bias is
 * controlled outside the LoRa transceiver (e.g. an analog PA biased through a DAC
 * pin, as on the RadioMaster Nomad Gemini).
 *
 * All four functions have weak no-op / pass-through defaults (see RadioExternalPa.cpp),
 * so boards without an external PA are completely unaffected. A board provides an
 * external PA by giving STRONG overrides (typically in its
 * src/platform/extra_variants/<board>/variant.cpp).
 *
 * Relationship to the existing TX_GAIN_LORA / LoRaFEMInterface mechanisms:
 *   - TX_GAIN_LORA assumes the chip output index is a non-negative dBm value.
 *   - LoRaFEMInterface models *digital* front-end modules (discrete enable pins).
 * An analog PA that needs the transceiver driven at negative dBm fits neither, so it
 * is handled here instead.
 */

/// Sentinel returned by radioExternalPaMapPower() when the board has no external PA.
#define RADIO_EXTERNAL_PA_NO_MAP INT8_MIN

/**
 * Map a desired *total* radiated output power to the transceiver chip output power.
 *
 * Called from RadioInterface::limitPower() AFTER regional/regulatory clamping, so
 * @p requestedTotalDbm is already the legal total output we want out of the antenna.
 * The override returns the chip output power (dBm, may be negative) that, combined
 * with the external PA gain, yields that total, and configures/stashes the PA bias.
 *
 * @param requestedTotalDbm desired total output power in dBm (already region-limited)
 * @param freqHz            operating frequency in Hz (for band-dependent PAs)
 * @return chip output power in dBm, or RADIO_EXTERNAL_PA_NO_MAP if no external PA
 */
int8_t radioExternalPaMapPower(int8_t requestedTotalDbm, float freqHz);

/// Engage the external PA bias for transmit (called just before a transmission).
void radioExternalPaTxEnable();

/// Drop the external PA bias for receive/idle (called when entering RX/standby).
void radioExternalPaRxIdle();

/// Power the external PA fully down (called when the radio goes to sleep).
void radioExternalPaSleep();
