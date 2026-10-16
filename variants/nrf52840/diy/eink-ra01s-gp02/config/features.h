// =============================================================================
// config/features.h — Module Feature Exclusions
// =============================================================================
// Controls which Meshtastic modules are compiled out (1 = exclude).
// Checked by configuration.h before any module is compiled.
// =============================================================================

#pragma once

// ---------------------------------------------------------------------------
// Enabled modules (explicitly not excluded)
// ---------------------------------------------------------------------------
#define MESHTASTIC_EXCLUDE_SIXKEYNAVINPUT 0 // 6-button polling driver
#define MESHTASTIC_EXCLUDE_I2C 0            // PCF8563 RTC on I2C
#define MESHTASTIC_EXCLUDE_POWER_FSM 0      // Required for BLE advertising

// ---------------------------------------------------------------------------
// NRF_APM: use nrfx_power_usbstatus_get() to detect USB VBUS state.
// Required for correct isPowered() / USB detection on nRF52840 without PMU.
// ---------------------------------------------------------------------------
#define NRF_APM

// ---------------------------------------------------------------------------
// USE_POWERSAVE: forces is_power_saving = true. Enable when bench-testing
// without a battery (ADC reads ~0 mV → false external-power detection).
// Battery IS connected on this board; keep commented.
// ---------------------------------------------------------------------------
// #define USE_POWERSAVE

// ---------------------------------------------------------------------------
// USE_SEGGER: redirect LOG_* output to SEGGER RTT (SWD/J-Link) instead of
// USB-CDC.  Enable when the device has no USB connected.
// ---------------------------------------------------------------------------
// #define USE_SEGGER

// ---------------------------------------------------------------------------
// BLE configuration
// ---------------------------------------------------------------------------
#define BLE_ADV_FAST_INTERVAL 800  // 800 × 0.625 ms = 500 ms
#define BLE_ADV_SLOW_INTERVAL 1600 // 1600 × 0.625 ms = 1000 ms
#define BLE_ADV_FAST_TIMEOUT_S 0   // Skip fast mode, use slow immediately

// ---------------------------------------------------------------------------
// No external sensors or motion hardware
// ---------------------------------------------------------------------------
#define MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR 1
#define MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR_EXTERNAL 1
#define MESHTASTIC_EXCLUDE_POWER_TELEMETRY 1
#define MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR 1
#define MESHTASTIC_EXCLUDE_HEALTH_TELEMETRY 1
#define MESHTASTIC_EXCLUDE_ACCELEROMETER 1
#define MESHTASTIC_EXCLUDE_MAGNETOMETER 1
#define MESHTASTIC_EXCLUDE_DETECTIONSENSOR 1

// ---------------------------------------------------------------------------
// No audio
// ---------------------------------------------------------------------------
#define MESHTASTIC_EXCLUDE_AUDIO 1

// ---------------------------------------------------------------------------
// No BLE PAX counting
// ---------------------------------------------------------------------------
#define MESHTASTIC_EXCLUDE_PAXCOUNTER 1

// ---------------------------------------------------------------------------
// No peripheral notification module
// ---------------------------------------------------------------------------
#define MESHTASTIC_EXCLUDE_EXTERNALNOTIFICATION 1

// ---------------------------------------------------------------------------
// Unused features and network modules
// ---------------------------------------------------------------------------
#define MESHTASTIC_EXCLUDE_MQTT 1           // nRF52 has no WiFi
#define MESHTASTIC_EXCLUDE_SERIAL 1         // Serial bridge module
#define MESHTASTIC_EXCLUDE_STOREFORWARD 1   // RAM-heavy, not needed
#define MESHTASTIC_EXCLUDE_DROPZONE 1       // File sharing
#define MESHTASTIC_EXCLUDE_ATAK 1           // ATAK integration
#define MESHTASTIC_EXCLUDE_REPLYBOT 1       // Auto-reply bot
#define MESHTASTIC_EXCLUDE_REMOTEHARDWARE 1 // Remote GPIO control
#define MESHTASTIC_EXCLUDE_POWERSTRESS 1    // Stress-test tool
