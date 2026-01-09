#define LED_PIN 18

#define _VARIANT_HELTEC_WIRELESS_TRACKER

// I2C
#define I2C_SDA SDA
#define I2C_SCL SCL

// ST7735S TFT LCD
#define ST7735S 1 // there are different (sub-)versions of ST7735
#define ST7735_CS 38
#define ST7735_RS 40  // DC
#define ST7735_SDA 42 // MOSI
#define ST7735_SCK 41
#define ST7735_RESET 39
#define ST7735_MISO -1
#define ST7735_BUSY -1
#define TFT_BL 21
#define ST7735_SPI_HOST SPI3_HOST
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define SCREEN_ROTATE
#define TFT_HEIGHT DISPLAY_WIDTH
#define TFT_WIDTH DISPLAY_HEIGHT
#define TFT_OFFSET_X 24
#define TFT_OFFSET_Y 0
#define TFT_INVERT false
#define SCREEN_TRANSITION_FRAMERATE 3 // fps
#define DISPLAY_FORCE_SMALL_FONTS
#define USE_TFTDISPLAY 1

#define VEXT_ENABLE 3 // active HIGH - powers the GPS, GPS LNA and OLED
#define VEXT_ON_VALUE HIGH
#define BUTTON_PIN 0

#define BATTERY_PIN 1 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define ADC_CHANNEL ADC1_GPIO1_CHANNEL
#define ADC_ATTENUATION ADC_ATTEN_DB_2_5 // lower dB for high resistance voltage divider
#define ADC_MULTIPLIER 4.9 * 1.045
#define ADC_CTRL 2     // active HIGH, powers the voltage divider.
#define ADC_USE_PULLUP // Use internal pullup/pulldown instead of actively driving the output

#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 33
#define GPS_TX_PIN 34
#define PIN_GPS_RESET 35
#define PIN_GPS_PPS 36
// #define PIN_GPS_EN 3    // Uncomment to power off the GPS with triple-click on Tracker v2, though we'll also lose the
// display.

#define GPS_RESET_MODE LOW
#define GPS_UC6580
#define GPS_BAUDRATE 115200

#define USE_SX1262
#define LORA_DIO0 -1 // a No connect on the SX1262 module
#define LORA_RESET 12
#define LORA_DIO1 14 // SX1262 IRQ
#define LORA_DIO2 13 // SX1262 BUSY
#define LORA_DIO3    // Not connected on PCB, but internally on the TTGO SX1262, if DIO3 is high the TCXO is enabled

#define LORA_SCK 9
#define LORA_MISO 11
#define LORA_MOSI 10
#define LORA_CS 8

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET

#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// ---- GC1109 RF FRONT END CONFIGURATION ----
// The Heltec Wireless Tracker V2 uses a GC1109 FEM chip with integrated PA and LNA
// RF path: SX1262 -> GC1109 PA -> Pi attenuator -> Antenna
// Measured net TX gain (non-linear due to PA compression):
//   +11dB at 0-15dBm input  (e.g., 10dBm in -> 21dBm out)
//   +10dB at 16-17dBm input
//   +9dB  at 18-19dBm input
//   +7dB  at 21dBm input    (e.g., 21dBm in -> 28dBm out max)
// Control logic (from GC1109 datasheet):
//   Shutdown:        CSD=0, CTX=X, CPS=X
//   Receive LNA:     CSD=1, CTX=0, CPS=X  (17dB gain, 2dB NF)
//   Transmit bypass: CSD=1, CTX=1, CPS=0  (~1dB loss, no PA)
//   Transmit PA:     CSD=1, CTX=1, CPS=1  (full PA enabled)
// Pin mapping:
//   CTX (pin 6)  -> SX1262 DIO2: TX/RX path select (automatic via SX126X_DIO2_AS_RF_SWITCH)
//   CSD (pin 4)  -> GPIO4: Chip enable (HIGH=on, LOW=shutdown)
//   CPS (pin 5)  -> GPIO46: PA mode select (HIGH=full PA, LOW=bypass)
//   VCC0/VCC1    -> Vfem via U3 LDO, controlled by GPIO7
#define USE_GC1109_PA
#define LORA_PA_POWER 7  // VFEM_Ctrl - GC1109 LDO power enable
#define LORA_PA_EN 4     // CSD - GC1109 chip enable (HIGH=on)
#define LORA_PA_TX_EN 46 // CPS - GC1109 PA mode (HIGH=full PA, LOW=bypass)

// GC1109 FEM: TX/RX path switching is handled by DIO2 -> CTX pin (via SX126X_DIO2_AS_RF_SWITCH)
// GPIO46 is CPS (PA mode), not TX control - setTransmitEnable() handles it in SX126xInterface.cpp
// Do NOT use SX126X_TXEN/RXEN as that would cause double-control of GPIO46