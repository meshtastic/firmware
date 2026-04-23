/*
  Variant definition for LilyGo T-Echo-Card (nRF52840)

  Pin mapping derived from the upstream vendor header:
    https://github.com/Xinyuan-LilyGO/T-Echo-Card/blob/main/libraries/private_library/t_echo_card_config.h

  This variant is flagged PRIVATE_HW while upstream hardware-model allocation is pending.
*/

#ifndef _VARIANT_T_ECHO_CARD_
#define _VARIANT_T_ECHO_CARD_

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32kHz crystal for LF

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// Number of pins defined in PinDescription array
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs - board only exposes 3x WS2812 addressable LEDs. No plain GPIO LEDs.
// Define a dummy PIN_LED1 so core code that unconditionally touches it stays happy.
#define PIN_LED1 (-1)
#define LED_STATE_ON 1

// Three independent WS2812 data lines (one LED per line, not a daisy chain).
// Each is driven as a 1-pixel NeoPixel by StatusLEDModule / ExternalNotification,
// mirroring how the ELECROW ThinkNode M1 assigns LED_POWER (red) and
// LED_NOTIFICATION (green) to plain GPIOs.
#define WS2812_DATA_1 (32 + 7)  // P1.7  - charge/heartbeat (red)
#define WS2812_DATA_2 (32 + 12) // P1.12 - external notification (green)
#define WS2812_DATA_3 (0 + 28)  // P0.28 - BLE pairing (blue)

// Wire each WS2812 to a status role. Colour defaults are scaled to 25%
// brightness (0x40) — the bare-die WS2812s on this board are very bright at
// full intensity in a close-range enclosure. Override here if the final
// enclosure needs a different tint or intensity.
#define NEOPIXEL_STATUS_POWER_PIN WS2812_DATA_1
#define NEOPIXEL_STATUS_NOTIFICATION_PIN WS2812_DATA_2
#define NEOPIXEL_STATUS_PAIRING_PIN WS2812_DATA_3
#define NEOPIXEL_STATUS_POWER_COLOR 0x400000        // red   @ 25%
#define NEOPIXEL_STATUS_NOTIFICATION_COLOR 0x004000 // green @ 25%
#define NEOPIXEL_STATUS_PAIRING_COLOR 0x000040      // blue  @ 25%

// The charger IC does not blink on its own; let StatusLEDModule do the
// software blink while charging (same semantics as not defining this on M1).
// If left defined: hardware would be expected to handle the charging pulse.
// #define POWER_LED_HARDWARE_BLINKS_WHILE_CHARGING

// Buttons
#define PIN_BUTTON1 (32 + 10) // KEY_1: P1.10

#define BUTTON_CLICK_MS 400

// Analog pins
#define PIN_A0 (0 + 2) // Battery ADC (BATTERY_ADC_DATA)

#define BATTERY_PIN PIN_A0

static const uint8_t A0 = PIN_A0;

#define ADC_RESOLUTION 14

// BATTERY_MEASUREMENT_CONTROL - enable divider for battery reading
#define ADC_CTRL (0 + 31)
#define ADC_CTRL_ENABLED HIGH

// NFC
#define PIN_NFC1 (9)
#define PIN_NFC2 (10)

// Wire Interfaces (IIC_1 on the vendor header)
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (32 + 4) // IIC_1_SDA: P1.4
#define PIN_WIRE_SCL (32 + 2) // IIC_1_SCL: P1.2

// External serial flash ZD25WQ32CEIGR
// QSPI Pins
#define PIN_QSPI_SCK (0 + 4)
#define PIN_QSPI_CS (0 + 12)
#define PIN_QSPI_IO0 (0 + 6)  // MOSI if using two bit interface
#define PIN_QSPI_IO1 (0 + 8)  // MISO if using two bit interface
#define PIN_QSPI_IO2 (32 + 9) // WP
#define PIN_QSPI_IO3 (0 + 26) // HOLD

// On-board QSPI Flash
#define EXTERNAL_FLASH_DEVICES ZD25WQ32CEIGR
#define EXTERNAL_FLASH_USE_QSPI

// Lora S62F (SX1262)
#define USE_SX1262
#define SX126X_CS (0 + 11)
#define SX126X_DIO1 (32 + 8)
#define SX126X_DIO2 (0 + 5)
#define SX126X_BUSY (0 + 14)
#define SX126X_RESET (0 + 7)
#define SX126X_RXEN (32 + 1) // SX1262_RF_VC2
#define SX126X_TXEN (0 + 27) // SX1262_RF_VC1
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// ───────────────────────────────────────────────────────────────────────────
// OLED display: SSD1315 on I2C @ 0x3C (IIC_1). SSD1315 is register-compatible
// with SSD1306, so USE_SSD1306 initializes the controller correctly.
//
// Viewport: the physical panel is 72×40, mapped into the SSD1315's 128×64
// GDDRAM at columns 28..99, pages 3..7 (rows 24..63). The firmware handles
// this by:
//   * asking the library for GEOMETRY_72_40, which sets the framebuffer to
//     72×40 and emits the right SETMULTIPLEX (39) / SETCOMPINS at init;
//   * relying on SSD1306Wire's built-in horizontal auto-centering
//     ((128 - width) / 2 = 28), so no horizontal shim is needed;
//   * calling SSD1306Wire::setYOffset(3) in Screen.cpp when
//     OLED_Y_OFFSET_PAGES is defined — this shifts every PAGEADDR write by
//     three pages (24 rows) so data lands on the visible rows.
//
// Precedent in the tree: variants/esp32c6/m5stack_unitc6l uses the same
// pattern but on SPI (USE_SPISSD1306 + GEOMETRY_64_48 + setHorizontalOffset).
// That layout happens to have its visible window on pages 0..5 of a 64×48
// OLED module, so no vertical shift is needed. Here we need +3 pages.
//
// ⚠ BUILD PREREQUISITE: GEOMETRY_72_40 and SSD1306Wire::setYOffset() are
//   not in the upstream library yet. Apply the diff at
//     variants/nrf52840/t-echo-card/oled_72x40_viewport.patch
//   to the lib_deps copy of meshtastic/esp8266-oled-ssd1306 before
//   building, or update the pinned commit in the root platformio.ini to a
//   fork/branch that contains it.
// ───────────────────────────────────────────────────────────────────────────
#define HAS_SCREEN 1
#define USE_SSD1306
#define OLED_GEOMETRY_OVERRIDE GEOMETRY_72_40 // read by main.cpp → Screen ctor
#define OLED_Y_OFFSET_PAGES 3                 // panel occupies pages 3..7
// Tiny OLED panel — opts into compile-time layout/font/feature substitutions
// gated on OLED_TINY across the graphics stack.
#define OLED_TINY

// Controls power 3V3 for all peripherals (GPS + LoRa + Sensor)
#define PIN_POWER_EN (0 + 30) // RT9080_EN

// SPI1 is unused (no external SPI display). Keep declarations for the core.
#define PIN_SPI1_MISO (-1)
#define PIN_SPI1_MOSI (-1)
#define PIN_SPI1_SCK (-1)

// GPS pins
#define GPS_L76K
#define GPS_BAUDRATE 9600
#define HAS_GPS 1

#define PIN_GPS_EN (32 + 15) // GPS_EN: P1.15 - GPS power enable
#define GPS_EN_ACTIVE 1
#define PIN_GPS_STANDBY (0 + 25) // GPS_WAKE_UP: P0.25 - wakeup pin
#define PIN_GPS_PPS (0 + 23)     // GPS_1PPS: P0.23
// GPS_{T,R}X_PIN follow Meshtastic's MCU-side convention (set via Uart::setPins
// in GPS.cpp), NOT the GPS chip's pin names. The vendor header labels these
// from the GPS's perspective: its TX output is on P0.19 and its RX input is on
// P0.21 — so the MCU must read from P0.19 and write to P0.21.
#define GPS_RX_PIN (0 + 19)    // MCU RX ← GPS's TX (vendor GPS_UART_TX / P0.19)
#define GPS_TX_PIN (0 + 21)    // MCU TX → GPS's RX (vendor GPS_UART_RX / P0.21)
#define PIN_GPS_RESET (0 + 29) // GPS_RF_EN: GPS RF enable / reset

#define GPS_THREAD_INTERVAL 50

#define PIN_SERIAL1_RX GPS_RX_PIN
#define PIN_SERIAL1_TX GPS_TX_PIN

// SPI Interfaces (LoRa on SPI0)
#define SPI_INTERFACES_COUNT 2

// For LORA, SPI 0
#define PIN_SPI_MISO (0 + 17)
#define PIN_SPI_MOSI (0 + 15)
#define PIN_SPI_SCK (0 + 13)

// Battery
// The battery sense is hooked to PIN_A0 (P0.2) via a divider controlled by ADC_CTRL.
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER (2.0F)

// Buzzer (PWM output, passive piezo)
#define PIN_BUZZER (32 + 6) // BUZZER_DATA: P1.6

// ───────────────────────────────────────────────────────────────────────────
// IMU: InvenSense ICM-20948 on IIC_1 @ 0x68 (SDA/SCL shared with display).
// Meshtastic auto-detects this in ScanI2CTwoWire and instantiates
// ICM20948Sensor. Nothing to declare here — it is picked up by the I²C scan
// (address 0x68 is already handled via ICM20948_ADDR_ALT in configuration.h)
// as long as HAS_SENSOR is on (nrf52 default) and the 3V3 rail
// (PIN_POWER_EN) is enabled before the scan runs.
// ───────────────────────────────────────────────────────────────────────────

// ───────────────────────────────────────────────────────────────────────────
// Magnetometer (optional external add-on): QMC5883P. Only referenced in the
// vendor's debug/examples. Not part of the main pin header, probably meant
// to be attached via the exposed I²C lines.
// ⚠ Meshtastic currently only supports QMC5883*L* (addr 0x0D) via
//    mprograms/QMC5883LCompass. QMC5883P is a different chip at 0x2C with
//    different registers — Adafruit_QMC5883P is the Arduino driver, but
//    Meshtastic has no detection entry or telemetry mapping for it yet.
// TODO: if the production board ships with a QMC5883P, add a ScanI2C
//       DeviceType + telemetry sensor and swap the library dependency.
// ───────────────────────────────────────────────────────────────────────────
// #define QMC5883P_ADDR 0x2C

// ───────────────────────────────────────────────────────────────────────────
// I²S speaker (MAX98357 Class-D amp). Stereo I²S data path.
// ⚠ Meshtastic's audio stack (AudioThread / AudioModule / codec2
//    walkie-talkie) is gated on ARCH_ESP32 + HAS_I2S and uses
//    ESP8266Audio / esp-idf I²S drivers. There is no nRF52 I²S backend.
// TODO to enable audio out on this board:
//   1. Add an nRF52-I²S driver wrapper around nrf_i2s (the Adafruit core
//      does not expose a clean Arduino API; the vendor uses a custom
//      Cpp_Bus_Driver::Hardware_Iis — see examples/play_music).
//   2. Port AudioThread to be architecture-agnostic (currently assumes the
//      ESP8266Audio library set).
//   3. Toggle SPEAKER_EN high, then SPEAKER_EN_2 if dual-rail is needed.
// Until then these defines exist for out-of-tree code only.
// ───────────────────────────────────────────────────────────────────────────
#define SPEAKER_EN (32 + 11)     // P1.11 - amp main enable
#define SPEAKER_EN_2 (0 + 3)     // P0.3  - secondary enable (vendor firmware toggles both)
#define SPEAKER_BCLK (0 + 16)    // P0.16 - I2S bit clock
#define SPEAKER_DATA (0 + 20)    // P0.20 - I2S data (SDOUT)
#define SPEAKER_WS_LRCK (0 + 22) // P0.22 - I2S word select / LRCK

// ───────────────────────────────────────────────────────────────────────────
// PDM microphone (ST MP34DT05).
// ⚠ No Meshtastic consumer for PDM input exists on any architecture.
//    The vendor's walkie-talkie demo does PDM → Codec2 → LoRa using its
//    own Cpp_Bus_Driver::Hardware_Pdm; that pipeline is not ported.
// TODO to enable a mic path:
//   1. Decide on a consumer (codec2 voice TX? voice announcements?).
//   2. Use Adafruit nRF52 core's built-in PDM.h wrapper (Arduino-compatible
//      API exists on nRF52840). Clock on MIC_SCLK, data on MIC_DATA.
// ───────────────────────────────────────────────────────────────────────────
#define MIC_SCLK (32 + 3) // P1.3 - PDM clock (MIC_SCLK on vendor header)
#define MIC_DATA (32 + 5) // P1.5 - PDM data (MIC_DATA on vendor header)

// ───────────────────────────────────────────────────────────────────────────
// NFC (NFC-A tag emulation via Nordic NFCT peripheral, pins P0.9/P0.10).
// ⚠ The Adafruit nRF52 Arduino core only exposes these pins as plain GPIO.
//    The vendor FAQ is explicit: NFC requires Nordic's SoftDevice SDK and
//    is not supported on the nRF52 Arduino platform. There is also no
//    NFC consumer anywhere in Meshtastic.
// PIN_NFC1 / PIN_NFC2 are already declared above for the core's pin table.
// ───────────────────────────────────────────────────────────────────────────

#define SERIAL_PRINT_PORT 0

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
