#pragma once

#define ARCH_NRF54L15

//
// Feature flags for nRF54L15
// BLE is deferred to Phase 2 — the nRF54L15 uses MPSL/Zephyr BLE APIs,
// not the Adafruit SoftDevice stack used by nRF52840.
//

#ifndef HAS_BLUETOOTH
#define HAS_BLUETOOTH 0
#endif
#ifndef HAS_SCREEN
#define HAS_SCREEN 0
#endif
#ifndef HAS_WIRE
#define HAS_WIRE 1
#endif
#ifndef HAS_GPS
#define HAS_GPS 0
#endif
#ifndef HAS_BUTTON
#define HAS_BUTTON 1
#endif
#ifndef HAS_TELEMETRY
#define HAS_TELEMETRY 1
#endif
#ifndef HAS_SENSOR
#define HAS_SENSOR 1
#endif
#ifndef HAS_RADIO
#define HAS_RADIO 1
#endif
#ifndef HAS_CPU_SHUTDOWN
#define HAS_CPU_SHUTDOWN 0
#endif
#ifndef HAS_CUSTOM_CRYPTO_ENGINE
#define HAS_CUSTOM_CRYPTO_ENGINE 0
#endif

// ADC reference — nRF54L15 SAADC uses VDD/4 internal ref by default
#ifndef AREF_VOLTAGE
#define AREF_VOLTAGE 3.6
#endif
#ifndef BATTERY_SENSE_RESOLUTION_BITS
#define BATTERY_SENSE_RESOLUTION_BITS 12
#endif

//
// HW_VENDOR — maps build-time define to HardwareModel enum
//
#ifdef NRF54L15_DK
#define HW_VENDOR meshtastic_HardwareModel_NRF54L15_DK
#else
#define HW_VENDOR meshtastic_HardwareModel_UNSET
#endif
