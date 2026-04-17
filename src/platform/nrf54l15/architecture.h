#pragma once

#define ARCH_NRF54L15

//
// Feature flags for nRF54L15.
//
// The HAS_* macros below are Meshtastic's compile-time feature gate: every
// optional subsystem (BLE, screen, I2C, GPS, buttons, telemetry, sensors,
// radio, CPU shutdown, ...) is wrapped in `#if HAS_FOO` so a given board
// only pays for the features it actually ships. On memory-tight MCUs this
// is not cosmetic — it's the difference between a binary that fits in
// flash and one that doesn't, and between a build that links and one that
// drags in drivers for hardware the board doesn't have. Defaulting to 0
// here (rather than inheriting nRF52 defaults) is deliberate: the
// nRF54L15-DK is a bare dev kit with no screen, no I2C sensors, no GPS,
// no user buttons — so every HAS_* flag starts off and gets flipped on
// explicitly by variants that add that hardware.
//
// Feature flags are also the cleanest way to absorb platform divergence
// without sprinkling `#ifdef ARCH_NRF54L15` across shared code. Anywhere
// a subsystem can be conditionally compiled via HAS_*, prefer that over
// per-arch guards: it keeps the core code arch-agnostic, makes it trivial
// to bring up the next board (flip the flags, don't patch call sites),
// and keeps the "does this platform support X?" question answerable by
// reading one file instead of grepping the tree. BLE in particular is
// deferred to Phase 2 on this port — the nRF54L15 uses MPSL/Zephyr BLE
// APIs rather than the Adafruit SoftDevice stack used by nRF52840 — so
// while HAS_BLUETOOTH defaults to 1, the actual implementation lives in
// NRF54L15Bluetooth.cpp behind its own Zephyr Kconfig gates.
//

#ifndef HAS_BLUETOOTH
#define HAS_BLUETOOTH 1
#endif
#ifndef HAS_SCREEN
#define HAS_SCREEN 0
#endif
#ifndef HAS_WIRE
#define HAS_WIRE 0
#endif
#ifndef HAS_GPS
#define HAS_GPS 0
#endif
#ifndef HAS_BUTTON
#define HAS_BUTTON 0
#endif
#ifndef HAS_TELEMETRY
#define HAS_TELEMETRY 0
#endif
#ifndef HAS_SENSOR
#define HAS_SENSOR 0
#endif
#ifndef HAS_RADIO
#define HAS_RADIO 1
#endif
#ifndef HAS_CPU_SHUTDOWN
#define HAS_CPU_SHUTDOWN 0
#endif

// ADC reference — nRF54L15 SAADC uses VDD/4 internal ref by default
#ifndef AREF_VOLTAGE
#define AREF_VOLTAGE 3.6
#endif
#ifndef BATTERY_SENSE_RESOLUTION_BITS
#define BATTERY_SENSE_RESOLUTION_BITS 12
#endif

//
// HW_VENDOR — maps build-time define to HardwareModel enum.
// Uses PRIVATE_HW until the Meshtastic protobufs assign a dedicated enum value
// for the nRF54L15-DK (the variant declares custom_meshtastic_hw_model = 132).
//
#ifdef NRF54L15_DK
#define HW_VENDOR meshtastic_HardwareModel_PRIVATE_HW
#else
#define HW_VENDOR meshtastic_HardwareModel_UNSET
#endif
