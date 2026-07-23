#pragma once

// Meshnology W12 "WiFi LoRa 32 V5" - ESP32-S3R8 (8 MB PSRAM) + 16 MB flash + Semtech LR2021
// dual-band LoRa (sub-GHz + 2.4 GHz) + SSD1315 0.96" OLED. Heltec V3-style pinout.
//
// Evidence: vendor pin sheet W12Pin_layout-20260209.xlsx ("WiFi LoRa 32 V5"), vendor Arduino
// demos (wiki.meshnology.com/W12), and on-hardware probing (I2C scan found only 0x3C at
// SDA17/SCL18; LR2021 answers on NSS=8 SCK=9 MISO=11 with BUSY=13/NRESET=12 behavior confirmed
// by reset-pulse probing).

// ─── OLED ─────────────────────────────────────────────────────────────────────
// SSD1315 (SSD1306-compatible) 128x64 at 0x3C
#define USE_SSD1306
#define I2C_SDA 17
#define I2C_SCL 18

// VEXT_EN powers the OLED/peripheral rail; active LOW (vendor Demo_08: HIGH = power off)
#define VEXT_ENABLE 45

// ─── User input ───────────────────────────────────────────────────────────────
#define BUTTON_PIN 0 // BOOT doubles as user button

// ─── Battery ──────────────────────────────────────────────────────────────────
// ADC_IN = GPIO1 through a divider (vendor Demo_06 assumes 2:1); GPIO2 monitors a second
// divider (solar/VUSB) and is not used here. No ADC control GPIO on this board.
#define BATTERY_PIN 1
#define ADC_CHANNEL ADC_CHANNEL_0 // GPIO1 = ADC1_CH0
#define ADC_MULTIPLIER 2.0

// ─── LoRa radio ───────────────────────────────────────────────────────────────
// Semtech LR2021 (dual-band). RF front-end switching is driven by the radio's dedicated
// CTX/CPS pins in hardware (vendor demo does no RF-switch DIO config), and the external PA
// enables (PA_EN_M=IO3 sub-GHz, PA_EN_G=IO4 2.4GHz) are pulled high on the board.
#define USE_LR2021
#define LORA_SCK 9
#define LORA_MISO 11
#define LORA_MOSI 10
#define LORA_CS 8
#define LORA_DIO1 14 // LR2021 DIO8 -> ESP32 IO14 (LORA_INT2); also the LoRa sleep-wake pin

#define LR2021_SPI_NSS_PIN LORA_CS
#define LR2021_IRQ_PIN LORA_DIO1
#define LR2021_NRESET_PIN 12
#define LR2021_BUSY_PIN 13
// Radio-side DIO wired to LR2021_IRQ_PIN. Verified on hardware by driving each DIO as a GPIO
// output and watching which ESP32 pin followed: DIO8 -> IO14 and DIO7 -> IO7. RadioLib defaults
// irqDioNum to 5, but DIO5 is not connected on this board - leaving it at 5 routes every
// interrupt to a floating pin, which makes the blocking scanChannel() wait forever.
#define LR2021_IRQ_DIO_NUM 8
// LR2021 DIO7 -> ESP32 IO7 (LORA_INT1, second IRQ line, unused)
// Plain crystal on XTA/XTB - no TCXO, so no LR2021_DIO3_TCXO_VOLTAGE

// ─── Not wired up yet ─────────────────────────────────────────────────────────
// TX1812-class RGB LED on GPIO46: enabling HAS_NEOPIXEL pulls the Arduino RMT HAL into the
// link, which fails against this build's trimmed FreeRTOS config - left disabled for now.
// External L76K GNSS header: ESP RX=39, TX=38, CTRL=48 (active low), WAKE=40, PPS=41, RST=42.
// No module is populated on-board, so GPS is not enabled here.
