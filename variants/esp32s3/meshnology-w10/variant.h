#pragma once

// Meshnology W10 "LoRa AIOT Dev Kit" - ESP32-S3R8 + EBYTE E22-900MM22S (SX1262) + AXP2101 PMIC +
// Quectel L76KB-A58 GPS + SPI TFT. The SX1262 RESET/DIO1/BUSY and the LCD reset are wired to an
// MCP23017 I2C expander (0x20) whose /INT is not routed to the MCU, so DIO1 uses a software poll.

// ─── I2C bus ──────────────────────────────────────────────────────────────────
// Shared by: AXP2101 PMIC, MCP23017 I/O expander, PCF85063ATL RTC, QMI8658 IMU,
//            ES8311 codec, SHT41 temp/humidity sensor (pg1: ESP_SDA=GPIO8, ESP_SCL=GPIO7)
#define I2C_SDA 8
#define I2C_SCL 7

// ─── Power management ─────────────────────────────────────────────────────────
// AXP2101 PMIC on I2C (pg3). AXP_IRQ → EXIO5 (expander, whose /INT is not routed to the
// ESP32), so no PMU_IRQ is possible; battery state is polled via the AXP2101 fuel gauge.
// There is no direct battery ADC - the PMIC is the only voltage source.
#define HAS_AXP2101

// ─── RTC ──────────────────────────────────────────────────────────────────────
// PCF85063ATL on I2C (pg3); RTC_INT → EXIO4 (unused)
#define PCF85063_RTC 0x51

// ─── I/O expander ─────────────────────────────────────────────────────────────
// U7 is drawn as "TCA9555PWR(UMW)" on schematic V1.1, but production boards use the MCP23017
// register map (V1.2 placement labels the part MCP23017T-E, and all vendor firmware/demo code
// drives IODIR 0x00/01, GPIO 0x12/13, OLAT 0x14/15). A0/A1/A2 = 0 → I2C address 0x20.
// The expander /INT output is NOT routed to the ESP32.
#define USE_MCP23017
#define MCP23017_ADDR 0x20
#define MCP23017_VPIN_BASE 100      // RadioLib virtual pins 100..115 = expander GPA0..GPB7
#define MCP23017_INT_ESP32_PIN (-1) // /INT not wired to any ESP32 GPIO
#if MCP23017_INT_ESP32_PIN < 0
// No hardware DIO1 interrupt possible: SX126x IRQ status register is polled from the radio thread
#define LORA_DIO1_SOFTWARE_POLL 1
#endif

// LORA_DIO1 is an expander pin, not an ESP32 GPIO, so it can't be used as a sleep/GPIO wakeup source
// (shared capability; see its use in sleep.cpp).
#define LORA_DIO1_EXTENDED_IO

// Expander pin map (pg2 "I/O Extensions"; EXIO0..7 = GPA0..7 / P00..P07, EXIO8..15 = GPB0..7 / P10..P17)
// Not wired up here: EXIO0 CAM_PWDN, EXIO2 TP_INT, EXIO5 AXP_IRQ, EXIO6 SYS_OUT,
//                    EXIO11 GPS RESET_N (driver FET Q3 unpopulated), EXIO13 TP_RST
#define EXIO_LCD_RST 1    // GPA1: LCD reset
#define EXIO_LORA_NRST 3  // GPA3: E22 NRST
#define EXIO_RTC_INT 4    // GPA4: PCF85063 INT (unused)
#define EXIO_PA_CTRL 7    // GPA7: NS4150 speaker amp enable (driven by AudioThread during playback)
#define EXIO_IMU_INT1 8   // GPB0: QMI8658 INT1 (input only, no MCU interrupt)
#define EXIO_LORA_DIO1 9  // GPB1: E22 DIO1
#define EXIO_LORA_BUSY 10 // GPB2: E22 BUSY
#define EXIO_GPS_WAKE 12  // GPB4: L76KB WAKE_UP (driven high at boot)

// ─── LoRa radio ───────────────────────────────────────────────────────────────
// EBYTE E22-900MM22S (SX1262) - pg4 U10. SPI shared with the LCD, separate chip selects.
// Bus pins via 0R links: E22_SCK=GPIO12, E22_MOSI=GPIO13, E22_MISO=GPIO11, E22_NSS=GPIO14 (pg4)
#define USE_SX1262
#define LORA_SCK 12
#define LORA_MOSI 13
#define LORA_MISO 11
#define LORA_CS 14

// Control lines route through the MCP23017 (pg4: E22_NRST=EXIO3, E22_DIO1=EXIO9, E22_BUSY=EXIO10),
// handled as RadioLib virtual pins by MCP23017LockingArduinoHal
#define LORA_DIO1 (MCP23017_VPIN_BASE + EXIO_LORA_DIO1)  // 109
#define LORA_BUSY (MCP23017_VPIN_BASE + EXIO_LORA_BUSY)  // 110
#define LORA_RESET (MCP23017_VPIN_BASE + EXIO_LORA_NRST) // 103

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET

// RF switch is fully hardware-automatic on this board: DIO2 drives TXEN directly and RXEN through
// inverting FET Q4 (pg4). Do not assign TXEN/RXEN GPIOs.
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_TXEN RADIOLIB_NC
#define SX126X_RXEN RADIOLIB_NC

// E22-900MM22S uses a plain 32 MHz crystal (no TCXO) - SX126X_DIO3_TCXO_VOLTAGE deliberately not defined
#define SX126X_MAX_POWER 22

// ─── Display ──────────────────────────────────────────────────────────────────
// SPI TFT on the "OLED"-silkscreened header / LCD FPC, sharing the LoRa SPI bus (pg1).
// Vendor ships two panels on identical pins; the default kit has the ST7789 1.54" IPS 240x240.
// Build with -D MESHNOLOGY_W10_LCD_ST7796_35 for the 3.5" ST7796 320x480 panel instead.
#define TFT_CS 10  // pg1: OLED_CS=GPIO10
#define TFT_DC 16  // pg1: OLED_DC=GPIO16
#define TFT_BL 6   // pg1: OLED_BL=GPIO6 (backlight PWM)
#define TFT_RST -1 // panel reset is EXIO1 on the expander, toggled in mcp23017EarlyInit()
#define HAS_SPI_TFT 1 // main.cpp keys SPI-TFT Screen creation on this since #10803
#define USE_TFTDISPLAY 1

#define SPI_FREQUENCY 75000000
#define SPI_READ_FREQUENCY 16000000

#ifdef MESHNOLOGY_W10_LCD_ST7796_35
// ST7796 3.5" 320x480 (capacitive touch on I2C - not enabled yet)
#define ST7796_SPI_HOST SPI2_HOST
#define ST7796_CS TFT_CS
#define ST7796_RS TFT_DC
#define ST7796_SDA LORA_MOSI
#define ST7796_SCK LORA_SCK
#define ST7796_MISO LORA_MISO
#define ST7796_RESET TFT_RST
#define ST7796_BL TFT_BL
#define ST7796_BUSY -1
#define TFT_WIDTH 320
#define TFT_HEIGHT 480
#define TFT_OFFSET_ROTATION 3
#else
// ST7789 1.54" IPS 240x240 (default kit panel)
#define ST7789_SPI_HOST SPI2_HOST
#define ST7789_CS TFT_CS
#define ST7789_RS TFT_DC
#define ST7789_SDA LORA_MOSI
#define ST7789_SCK LORA_SCK
#define ST7789_MISO LORA_MISO
#define ST7789_RESET TFT_RST
#define ST7789_BL TFT_BL
#define ST7789_BUSY -1
#define TFT_WIDTH 240
#define TFT_HEIGHT 240
#define TFT_OFFSET_ROTATION 1
#endif
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0

// ─── GPS ──────────────────────────────────────────────────────────────────────
// Quectel L76KB-A58 on UART0 pins (pg4: GPS_TXD→U0RXD=GPIO44, GPS_RXD→U0TXD=GPIO43; console is USB)
// 1PPS only drives LED3 (pg3); RESET_N driver FET is unpopulated; WAKE_UP = EXIO12, driven high at boot
#define HAS_GPS 1
#define GPS_RX_PIN 44
#define GPS_TX_PIN 43
#define GPS_BAUDRATE 9600

// ─── User input ───────────────────────────────────────────────────────────────
// pg1: SW2 pulls GPIO0 to GND (BOOT doubles as user button). SW3 is the AXP2101 power button.
#define BUTTON_PIN 0
#define BUTTON_NEED_PULLUP

// ─── LED ──────────────────────────────────────────────────────────────────────
// TX1812 (WS2812-compatible) RGB LED on GPIO48 via 33R (pg1, U35). The other LEDs are
// hardware-driven: AXP2101 CHGLED, VSYS power LED, GPS 1PPS LED, UART0 TX/RX activity LEDs.
// TODO: enabling HAS_NEOPIXEL currently fails to link (Adafruit NeoPixel pulls in the Arduino RMT
// HAL, which needs xEventGroupSetBitsFromISR - not present in this build's FreeRTOS config)
// #define HAS_NEOPIXEL
// #define NEOPIXEL_COUNT 1
// #define NEOPIXEL_DATA 48
// #define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)

// ─── Audio ────────────────────────────────────────────────────────────────────
// ES8311 codec (I2C 0x18) -> NS4150 amp -> speaker, initialized in the variant's lateInitVariant().
// Used for notification tones / ringtones over the I2S "buzzer" path (turn on the
// use_i2s_as_buzzer external-notification option). Codec2 voice is SX1280-only, so it does not
// apply to this sub-GHz board. The NS4150 amp enable is on the MCP23017 (EXIO_PA_CTRL / GPA7) and
// is toggled by AudioThread around playback. pg3 I2S wiring below.
#define HAS_I2S
#define DAC_I2S_MCLK 1 // pg3: ES8311 MCLK
#define DAC_I2S_BCK 2  // pg3: ES8311 BCLK/SCLK
#define DAC_I2S_WS 4   // pg3: ES8311 LRCK
#define DAC_I2S_DOUT 5 // pg3: playback data (ESP32 -> ES8311)
#define DAC_I2S_DIN 3  // pg3: record data (ES8311 -> ESP32)
// AudioThread powers the NS4150 amp on/off around playback via this (opt-in) hook.
#define AUDIO_AMP_ENABLE(on) mcpIoExpander.digitalWrite(EXIO_PA_CTRL, (on) ? HIGH : LOW)

// ─── On-board peripherals not wired up yet ────────────────────────────────────
// SHT41 temp/humidity (0x44) and QMI8658 IMU (0x6B): auto-detected on the I2C scan
// microSD: CS on GPIO9, shares the LCD/LoRa SPI bus - not enabled
// Camera interface: GPIO38-42/45-48 (pg1) - not enabled

// ─── Board identity ───────────────────────────────────────────────────────────
#define MESHNOLOGY_W10 1
