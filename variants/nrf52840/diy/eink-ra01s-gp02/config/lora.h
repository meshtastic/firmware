// =============================================================================
// config/lora.h — LoRa Radio Driver Selection & Tuning
// =============================================================================
// Maps variant.h PIN_LORA_* names to the SX126X_* names expected by the
// Meshtastic SX126xInterface / RadioInterface framework.
// =============================================================================

#pragma once

#include "variant.h"

// ---------------------------------------------------------------------------
// Radio module selection
// The RA-01S physically contains an SX1268 (China-band); its register map is
// identical to SX1262.  USE_SX1262 is correct — do NOT use USE_SX1268.
// ---------------------------------------------------------------------------
#define USE_SX1262

// ---------------------------------------------------------------------------
// Framework pin aliases (SX126xInterface constructor args)
// ---------------------------------------------------------------------------
#define SX126X_CS PIN_LORA_CS
#define SX126X_DIO1 PIN_LORA_DIO1
#define SX126X_RESET PIN_LORA_RST
#define SX126X_BUSY PIN_LORA_BUSY

// ---------------------------------------------------------------------------
// Power enable — RA-01S LDO on P0.13 (active-high)
// ---------------------------------------------------------------------------
#define SX126X_POWER_EN PIN_LORA_PWR

// ---------------------------------------------------------------------------
// RF switch — RA-01S uses DIO2 internally for TX/RX antenna switching
// No external MCU pin required; RadioLib handles it via SetDio2AsRfSwitchCtrl.
// ---------------------------------------------------------------------------
#define SX126X_DIO2_AS_RF_SWITCH

// ---------------------------------------------------------------------------
// TCXO — NOT present on RA-01S (crystal oscillator variant).
// Do NOT define SX126X_DIO3_TCXO_VOLTAGE.
// (Uncomment only for RA-01SH which has a TCXO.)
// ---------------------------------------------------------------------------
// #define SX126X_DIO3_TCXO_VOLTAGE  1.8

// ---------------------------------------------------------------------------
// Maximum TX power (dBm)
// ---------------------------------------------------------------------------
#define SX126X_MAX_POWER 22
