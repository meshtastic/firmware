/*
 * Vigil Acoustic C-UAS Sensor Node — Variant Configuration
 *
 * Based on Heltec WiFi LoRa 32 V4 (ESP32-S3 + SX1262)
 * Extended with: 16x ICS-52000 MEMS mic array via I2S TDM
 *                QMC5883L magnetometer via I2C1
 *                Analog comparator for wake-on-sound
 *                Piezo buzzer for drone alarm
 *                RGB LED for status indication
 *
 * Pin budget (ESP32-S3 has GPIO 0-48):
 *
 *   ┌─────────────────────────────────────────────────┐
 *   │  LORA (SX1262)    : 8,9,10,11,12,13,14  (7 pins)│
 *   │  LORA FEM         : 2,5,7,46            (4 pins)│
 *   │  GPS (L76K)       : 34,38,39,40,41,42   (6 pins)│
 *   │  OLED I2C0        : 17,18,21            (3 pins)│
 *   │  I2C1 (mag+spare) : 3,4                 (2 pins)│
 *   │  I2S TDM (mics)   : 6,15,16,19,20,43   (6 pins)│
 *   │  Wake comparator  : 44                  (1 pin) │
 *   │  Piezo buzzer     : 45                  (1 pin) │
 *   │  RGB LED          : 47,48,35            (3 pins)│
 *   │  ADC/battery      : 1,36,37             (3 pins)│
 *   │  USB              : 19,20 (shared)              │
 *   │  Button           : 0                   (1 pin) │
 *   └─────────────────────────────────────────────────┘
 *   Total: 37/49 GPIO used
 */

// ---- Inherit Heltec V4 base definitions ----
// LoRa, GPS, OLED, battery ADC pins inherited from heltec_v4 variant.h
#include "../../heltec_v4/variant.h"

// ---- I2S TDM Bus (16x ICS-52000 MEMS microphones) ----
// Architecture decision #18: 1024-sample DMA double buffer
// 4 data lines x 4 TDM slots = 16 channels
#define VIGIL_I2S_BCLK    6    // Bit clock (shared by all mics)
#define VIGIL_I2S_WS      15   // Word select / LRCLK
#define VIGIL_I2S_DIN0    16   // Data line 0: channels 0-3
#define VIGIL_I2S_DIN1    19   // Data line 1: channels 4-7
#define VIGIL_I2S_DIN2    20   // Data line 2: channels 8-11
#define VIGIL_I2S_DIN3    43   // Data line 3: channels 12-15
#define VIGIL_I2S_SAMPLE_RATE   48000
#define VIGIL_I2S_TDM_SLOTS     16

// ---- I2C1 Bus (QMC5883L magnetometer + expansion) ----
// Using Heltec V4's secondary I2C bus
#define VIGIL_MAG_I2C_SDA  4
#define VIGIL_MAG_I2C_SCL  3
#define VIGIL_MAG_I2C_ADDR 0x0D  // QMC5883L default

// ---- Wake-on-Sound (analog comparator input) ----
// Architecture decision #8: hybrid wake = comparator + 1s periodic
#define VIGIL_WAKE_COMP_PIN  44  // Analog comparator input from mic preamp
#define VIGIL_WAKE_THRESHOLD 512 // ADC threshold (~50dB SPL)

// ---- Piezo Buzzer (drone alarm) ----
#define VIGIL_PIEZO_PIN  45
#define VIGIL_PIEZO_FREQ 4000  // Also used for acoustic calibration chirps

// ---- RGB Status LED ----
// Green=idle, Amber=calibrating, Red=drone detected, Blue=mesh activity
#define VIGIL_LED_R  47
#define VIGIL_LED_G  48
#define VIGIL_LED_B  35

// ---- Core Assignment ----
// Architecture decision #11: DSP on Core 0, Meshtastic on Core 1
#define VIGIL_DSP_CORE     0
#define VIGIL_MESH_CORE    1

// ---- Enable Vigil Module ----
#define HAS_VIGIL 1
