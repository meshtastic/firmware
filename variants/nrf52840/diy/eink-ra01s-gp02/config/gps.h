// =============================================================================
// config/gps.h — GNSS Driver Configuration
// =============================================================================
// Maps variant.h PIN_GNSS_* names to the PIN_GPS_* names expected by the
// Meshtastic GPS framework (GPS.cpp / GPS.h).
//
// Standby pin (P1.07): active-low, internal pull-up ensures HIGH (normal
// operation) by default.  GPS_STANDBY_ACTIVE keeps its default value of LOW
// (see GPS.h), so writePinStandby(true) drives the pin LOW → standby.
// =============================================================================

#pragma once

#include "variant.h"

// ---------------------------------------------------------------------------
// Framework pin aliases
// ---------------------------------------------------------------------------
#define GPS_RX_PIN PIN_GNSS_RX
#define GPS_TX_PIN PIN_GNSS_TX
#define PIN_GPS_RESET PIN_GNSS_RST
#define PIN_GPS_EN PIN_GNSS_PWR
#define PIN_GPS_STANDBY PIN_GNSS_STBY
#define PIN_GPS_PPS PIN_GNSS_PPS

#define GPS_EN_ACTIVE GNSS_PWR_ACTIVE
#define GPS_STANDBY_ACTIVE GNSS_STBY_ACTIVE

// ---------------------------------------------------------------------------
// Baud rate — GP-02 default is 9600 bps (matches Meshtastic framework default)
// Only define GPS_BAUDRATE if a non-default rate is needed.
// ---------------------------------------------------------------------------
// #define GPS_BAUDRATE  9600
