// =============================================================================
// config/battery.h — Battery ADC Calibration & OCV Curve
// =============================================================================
// Hardware:
//   ADC input   : P0.04 (AIN2) → Arduino pin 4
//   Divider     : R1 = R2 = 2 kΩ  →  V_adc = V_batt × 0.5  → ADC_MULTIPLIER = 2.0
//   Power ctrl  : P0.15, active-high (HIGH = divider enabled)
//   Resolution  : 14-bit SAADC
//   Reference   : Internal 0.6 V ref × (1/6 gain) = 3.6 V full-scale (NOT VDD)
//
// Meshtastic uses AnalogBatteryLevel for voltage readback.
// See Power.cpp for the ADC burst sequence (adcEnable → analogRead → adcDisable).
// =============================================================================

#pragma once

// ---------------------------------------------------------------------------
// ADC input pin (must match PIN_BATTERY_ADC in variant.h)
// ---------------------------------------------------------------------------
#define BATTERY_PIN 4                    // P0.04 = AIN2 (Arduino pin 4)
#define ADC_MULTIPLIER 2.0f              // 50:50 divider: V_batt = V_adc × 2
#define BATTERY_SENSE_RESOLUTION_BITS 14 // nRF52 SAADC 14-bit mode
static const uint8_t A0 = BATTERY_PIN;
#define ADC_RESOLUTION 14

// ---------------------------------------------------------------------------
// Voltage-divider power-enable (active-high)
// P0.15 = Arduino pin 15.
// Power.cpp guards every analogRead() burst with adcEnable/adcDisable.
// ---------------------------------------------------------------------------
#define ADC_CTRL (0 + 15)     // P0.15
#define ADC_CTRL_ENABLED HIGH // active-high

// ---------------------------------------------------------------------------
// ADC reference voltage
// Adafruit nRF52 BSP configures SAADC with internal 0.6 V reference
// and 1/6 gain → full-scale = 0.6 V / (1/6) = 3.6 V.
// Using 3.3 would cause a systematic ~9 % under-read.
// ---------------------------------------------------------------------------
#define AREF_VOLTAGE 3.6

// ---------------------------------------------------------------------------
// OCV (Open-Circuit Voltage) discharge curve — standard single-cell LiPo
// Values in mV, ordered 100 % → 0 % SOC (11 equally-spaced points).
// ---------------------------------------------------------------------------
#define NUM_OCV_POINTS 11
#define OCV_ARRAY 4190, 4050, 3990, 3890, 3800, 3720, 3630, 3530, 3420, 3300, 3100
#define NUM_CELLS 1
