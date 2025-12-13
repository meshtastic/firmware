/**
 * @file variant.h
 * @brief Variant configuration for Seeed XIAO RP2350 with FRAM on SPI1
 *
 * XIAO RP2350 Pinout Reference:
 * - GPIO0:  D0 (TX/I2C SDA secondary)
 * - GPIO1:  D1 (RX/I2C SCL secondary)
 * - GPIO2:  D2
 * - GPIO3:  D3 (Available for FRAM CS)
 * - GPIO4:  D4 (I2C SDA default - SDA0)
 * - GPIO5:  D5 (I2C SCL default - SCL0)
 * - GPIO6:  D6 (I2C SDA1)
 * - GPIO7:  D7 (I2C SCL1)
 * - GPIO8:  D8 (SPI0 SCK)
 * - GPIO9:  D9 (SPI0 MISO)
 * - GPIO10: D10 (SPI0 MOSI / SPI1 SCK)
 * - GPIO26: A0 (ADC)
 * - GPIO27: A1 (ADC)
 * - GPIO28: A2 (ADC)
 * - GPIO29: A3 (ADC)
 *
 * SPI1 Configuration for FRAM:
 * - GPIO10: SPI1_SCK
 * - GPIO11: SPI1_TX (MOSI)
 * - GPIO12: SPI1_RX (MISO)
 * - GPIO3:  FRAM CS (configurable)
 */

#pragma once

#define ARDUINO_ARCH_AVR

// Board identification
#ifndef PRIVATE_HW
#define PRIVATE_HW
#endif

// ============================================================================
// I2C Configuration
// ============================================================================
// Default I2C0 pins (Wire)
#ifndef PIN_WIRE0_SDA
#define PIN_WIRE0_SDA 4
#endif
#ifndef PIN_WIRE0_SCL
#define PIN_WIRE0_SCL 5
#endif

// Secondary I2C1 pins (Wire1)
#ifndef PIN_WIRE1_SDA
#define PIN_WIRE1_SDA 6
#endif
#ifndef PIN_WIRE1_SCL
#define PIN_WIRE1_SCL 7
#endif

// ============================================================================
// SPI1 Configuration for FRAM
// ============================================================================
// XIAO RP2350 SPI1 pins
#ifndef PIN_SPI1_SCK
#define PIN_SPI1_SCK 10
#endif
#ifndef PIN_SPI1_MOSI
#define PIN_SPI1_MOSI 11
#endif
#ifndef PIN_SPI1_MISO
#define PIN_SPI1_MISO 12
#endif

// FRAM Chip Select pin (directly connected to XIAO)
#ifndef FRAM_CS_PIN
#define FRAM_CS_PIN 3
#endif

// FRAM SPI clock frequency (up to 20MHz supported by most FRAM chips)
#ifndef FRAM_SPI_FREQ
#define FRAM_SPI_FREQ 8000000
#endif

// Enable FRAM batch storage feature
#define HAS_FRAM_STORAGE 1

// FRAM size configuration (default 256Kbit = 32KB)
// Override for different FRAM chips:
// - FM25L16B: 2KB    -> #define FRAM_SIZE_BYTES 2048
// - FM25L64B: 8KB    -> #define FRAM_SIZE_BYTES 8192
// - FM25L256B: 32KB  -> #define FRAM_SIZE_BYTES 32768
// - FM25V10: 128KB   -> #define FRAM_SIZE_BYTES 131072
#ifndef FRAM_SIZE_BYTES
#define FRAM_SIZE_BYTES 32768
#endif

// ============================================================================
// SPI0 Configuration for LoRa (if used)
// ============================================================================
#define USE_SX1262

#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

// Default SPI0 for LoRa radio (adjust based on your wiring)
#define LORA_SCK 8
#define LORA_MISO 9
#define LORA_MOSI 10
#define LORA_CS 2

#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET 1
#define LORA_DIO1 0
#define LORA_DIO2 28
#define LORA_DIO3 RADIOLIB_NC

#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif

// ============================================================================
// GPIO Configuration
// ============================================================================
// User LED (XIAO RP2350 has RGB LED on GPIO16/17/25)
#define LED_PIN 25

// User button (optional, directly connected)
#ifndef BUTTON_PIN
#define BUTTON_PIN 27
#endif

// External notification pin
#define EXT_NOTIFY_OUT 26

// ============================================================================
// ADC Configuration
// ============================================================================
#define BATTERY_PIN 26
#define ADC_MULTIPLIER 3.1
#define BATTERY_SENSE_RESOLUTION_BITS ADC_RESOLUTION

// ============================================================================
// Serial Configuration
// ============================================================================
// Recommended pins for SerialModule
// txd = 0 (GPIO0/D0)
// rxd = 1 (GPIO1/D1)
