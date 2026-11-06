// =============================================================================
// config/led.h — LED Role → Meshtastic Name Mapping
// =============================================================================
// Maps physical LED pins (defined in variant.h) to Meshtastic framework
// LED roles.  Keeps hardware pin names stable; only this mapping changes
// if a different LED function assignment is desired.
//
// PIN_LED1/2/3 are intentionally NOT defined to avoid framework auto-mapping
// in nrf52/architecture.h (which would reclaim the red LED via LED_POWER).
// We use private names and explicitly define the roles below.
// =============================================================================

#pragma once

#include "variant.h"

// ---------------------------------------------------------------------------
// Role assignments
// ---------------------------------------------------------------------------
#define LED_PAIRING PIN_LED_2
#define LED_MSG PIN_LED_1
#define LED_EINK_ACTIVITY PIN_LED_0
