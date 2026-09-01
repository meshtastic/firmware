#pragma once

#include <stdint.h>

// Hooks for boards with an analog external PA that needs negative chip dBm (unlike
// TX_GAIN_LORA/LoRaFEMInterface). Weak no-op defaults; variants provide strong overrides.

/// Sentinel returned by radioExternalPaMapPower() when the board has no external PA.
#define RADIO_EXTERNAL_PA_NO_MAP INT8_MIN

/// Map a desired total output (dBm, already region-clamped) to chip output power and
/// configure the PA bias. Returns chip dBm or RADIO_EXTERNAL_PA_NO_MAP without a PA.
int8_t radioExternalPaMapPower(int8_t requestedTotalDbm, float freqMhz);

/// Engage the external PA bias for transmit (called just before a transmission).
void radioExternalPaTxEnable();

/// Drop the external PA bias for receive/idle (called when entering RX/standby).
void radioExternalPaRxIdle();

/// Power the external PA fully down (called when the radio goes to sleep).
void radioExternalPaSleep();
