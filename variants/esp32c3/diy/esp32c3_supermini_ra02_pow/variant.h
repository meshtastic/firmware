#pragma once

// Pins verified on two physical ESP32-C3 Super Mini + Ra-02 boards.
// This variant uses the upstream Meshtastic RF95/SX127x implementation.
// the RF packets and BLE API come solely from official Meshtastic firmware.
//
// Ra-02 (SX1278)       ESP32-C3 Super Mini
// -----------------------------------------
// NSS/CS               GPIO10
// SCK                  GPIO6
// MOSI                 GPIO7
// MISO                 GPIO5
// RESET                GPIO3
// DIO0 (RxDone/TxDone) GPIO4
// DIO1                 GPIO0 (wired, may be used by RF95 driver)
// VCC                  3.3 V (never 5 V)
// GND                  GND

// Ai-Thinker Ra-02 uses an SX1278 433 MHz-band radio.
//
// No region is hardcoded here. Users must configure the
// appropriate region through the Meshtastic app or CLI.
//
// The selected region determines frequency, duty-cycle,
// and transmit-power limits.
//
// This hardware supports only frequencies within its
// actual radio module and RF matching capabilities.

#define HAS_SCREEN 0
#define HAS_GPS 0

// Super Mini BOOT button is available for user input (GPIO9); not radio wiring.
#define BUTTON_PIN 9

// This module is SX1278, *not* SX1262/SX1268/LLCC68.
// Meshtastic calls the SX127x implementation RF95.
#define USE_RF95

#define LORA_CS 10
#define LORA_SCK 6
#define LORA_MOSI 7
#define LORA_MISO 5
#define LORA_RESET 3
#define LORA_DIO0 4
#define LORA_DIO1 0
#define LORA_DIO2 RADIOLIB_NC

// No separate PA/LNA control GPIOs on the Ai-Thinker Ra-02 module.
#define RF95_RXEN RADIOLIB_NC
#define RF95_TXEN RADIOLIB_NC

// There is no wired I2C display or GPS. Keep I2C off of DIO1 (GPIO0).
// Pins below are unused placeholders for optional devices, not connected now.
#define I2C_SDA 1
#define I2C_SCL 2
