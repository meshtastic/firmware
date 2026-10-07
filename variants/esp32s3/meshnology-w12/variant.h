#pragma once

// Meshnology W12 "WiFi LoRa 32 V5" - ESP32-S3R8 (8 MB PSRAM) + 16 MB flash + Semtech LR2021
// dual-band LoRa (sub-GHz + 2.4 GHz) + SSD1315 0.96" OLED. Heltec V3-style pinout.

// ─── OLED ─────────────────────────────────────────────────────────────────────
// SSD1315 (SSD1306-compatible) 128x64 at 0x3C
#define USE_SSD1306
#define I2C_SDA 17
#define I2C_SCL 18
#define BICOLOR_OLED_DISPLAY

// Powers the OLED/peripheral rail; active LOW
#define VEXT_ENABLE 45

// ─── User input ───────────────────────────────────────────────────────────────
#define BUTTON_PIN 0 // BOOT doubles as user button

// ─── Battery ──────────────────────────────────────────────────────────────────
// GPIO2 gates a P-MOSFET high-side switch on the divider (schematic W12-MB-V0.2 sheet 1); an NPN
// inverts it, so it is active HIGH. Undriven, the switch stays off and GPIO1 reads a hard 0 mV.
#define BATTERY_PIN 1
#define ADC_CHANNEL ADC_CHANNEL_0 // GPIO1 = ADC1_CH0
#define ADC_CTRL 2
#define ADC_CTRL_ENABLED HIGH
#define ADC_ATTENUATION ADC_ATTEN_DB_2_5 // 4.2V cell is only ~857mV after the divider
#define ADC_MULTIPLIER (490.0 / 100.0)   // R50 390K + R51 100K, over R51

// ─── LoRa radio ───────────────────────────────────────────────────────────────
// RF switching is hardwired to the radio's CTX/CPS pins, and the external PA enables
// (IO3 sub-GHz, IO4 2.4GHz) are pulled high, so no RF-switch DIO table is needed.
#define USE_LR2021
#define LORA_SCK 9
#define LORA_MISO 11
#define LORA_MOSI 10
#define LORA_CS 8
#define LORA_DIO1 14 // radio DIO8; also the LoRa sleep-wake pin

#define LR2021_SPI_NSS_PIN LORA_CS
#define LR2021_IRQ_PIN LORA_DIO1
#define LR2021_NRESET_PIN 12
#define LR2021_BUSY_PIN 13
// Must be 8, not RadioLib's default of 5: DIO5 is not bonded out here, so the default
// points every interrupt at a floating pin and scanChannel() never returns.
#define LR2021_IRQ_DIO_NUM 8
// DIO7 -> IO7 is a second, unused IRQ line. Plain crystal on XTA/XTB, so no TCXO voltage.

// ─── Not wired up yet ─────────────────────────────────────────────────────────
// RGB LED on GPIO46 stays off: HAS_NEOPIXEL pulls in the RMT HAL, which fails to link here.
// L76K GNSS header (RX=39, TX=38, CTRL=48 active low, WAKE=40, PPS=41, RST=42) is unpopulated.
