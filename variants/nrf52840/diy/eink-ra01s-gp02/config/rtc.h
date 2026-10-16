// =============================================================================
// config/rtc.h — RTC Configuration (PCF8563)
// =============================================================================
// PCF8563 on I2C bus (P0.26 SDA / P0.27 SCL, address 0x51).
// No INT pin connected; no alarm functionality used.
// CLKOUT (P0.16) is not used by Meshtastic.
// =============================================================================

#pragma once

// ---------------------------------------------------------------------------
// PCF8563 I2C address
// Enables the PCF8563_RTC branch in ScanI2CTwoWire.cpp + RTC.cpp:
//   - readFromRTC(): reads datetime → RTCQualityDevice on boot
//   - perhapsSetRTC(): writes datetime back when GPS/NTP improves quality
// ---------------------------------------------------------------------------
#define HAS_RTC 1
#define PCF8563_RTC 0x51
