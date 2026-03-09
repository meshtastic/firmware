// Touch_LCD_2_SX1262_L76K ESP32-S3 board configuration
// Hardware:
// - MCU: ESP32-S3
// - Display: 2-inch 240x320 ST7789 TFT LCD
// - Touch: CST816D capacitive touch controller
// - LoRa: SX1262 RF chip
// - GPS: L76K module

#ifndef __TOUCH_LCD_2_SX1262_L76K_VARIANT_H__
#define __TOUCH_LCD_2_SX1262_L76K_VARIANT_H__

// #define LED_PIN 5                        // LED not connected

// I2C configuration (touchscreen)
#define I2C_SDA 48 // I2C data line
#define I2C_SCL 47 // I2C clock line

// ST7789 TFT LCD configuration (2-inch 240x320)
#define USE_TFTDISPLAY 1 // Enable TFT display support
// #define HAS_TFT                          // Used to enable certain features, do not enable
// #define USE_ST7789                       // Used to enable certain features, do not enable
// #define VTFT_LEDA (1)                    // Backlight control pin, required by above macros, do not enable
// #define VTFT_CTRL (9)                    // TFT power control pin, required by above macros, do not enable
// #define ST7789_NSS (ST7789_CS)           // Chip select pin, required by above macros, do not enable
#define ST7789_CS (45)              // Chip select pin
#define ST7789_RS (42)              // Data/command select pin (DC)
#define ST7789_SDA (38)             // MOSI data line
#define ST7789_SCK (39)             // Clock line
#define ST7789_MISO (-1)            // MISO pin (can be set to -1)
#define ST7789_RESET (-1)           // Reset pin (not used by library)
#define ST7789_BUSY (-1)            // BUSY pin (not used by library)
#define ST7789_BL (1)               // Backlight control pin
#define ST7789_SPI_HOST SPI3_HOST   // SPI2 is occupied by LoRa by default
#define SPI_FREQUENCY 40000000      // 40MHz SPI frequency
#define SPI_READ_FREQUENCY 16000000 // 16MHz read frequency

// Screen parameters
#define TFT_HEIGHT 320                // TFT screen height in pixels, vertical resolution of 2-inch display
#define TFT_WIDTH 240                 // TFT screen width in pixels, horizontal resolution of 2-inch display
#define TFT_OFFSET_X 0                // Screen X-axis offset for display calibration
#define TFT_OFFSET_Y 0                // Screen Y-axis offset for display calibration
#define TFT_OFFSET_ROTATION 0         // Screen rotation offset, 0 = no rotation
#define SCREEN_ROTATE                 // Enable screen rotation, supports landscape and portrait modes
#define SCREEN_TRANSITION_FRAMERATE 5 // 5fps transition animation
#define BRIGHTNESS_DEFAULT 130        // Default brightness

// TFT color configuration (to suppress warnings)
#define TFT_MESH_OVERRIDE COLOR565(0x67, 0xEA, 0x94) // Meshtastic green theme
#define TFT_BLACK COLOR565(0x00, 0x00, 0x00)         // Black

// Touchscreen configuration (CST816D)
#define HAS_TOUCHSCREEN 1
#define SCREEN_TOUCH_INT 46      // Touch interrupt pin
#define TOUCH_I2C_PORT 0         // I2C port
#define TOUCH_SLAVE_ADDRESS 0x15 // CST816D I2C address (0x15)
// #define WAKE_ON_TOUCH                    // Enable touch interrupt wake-up

// Button configuration
#define BUTTON_PIN 0              // User button
#define BUTTON_ACTIVE_LOW true    // Button active low
#define BUTTON_ACTIVE_PULLUP true // Enable internal pull-up resistor

// Power management
// #define VEXT_ENABLE 16                   // External power enable pin not connected
// #define VEXT_ON_VALUE HIGH               // External power enable pin active level
#define BATTERY_PIN 5                   // Battery voltage sense pin
#define ADC_CHANNEL ADC1_GPIO5_CHANNEL  // Battery voltage ADC channel
#define ADC_ATTENUATION ADC_ATTEN_DB_12 // Battery ADC attenuation (12dB, 0-3.3V range)
#define ADC_MULTIPLIER 3.0              // Voltage divider ratio (1:3 divider)

// LoRa module configuration
#define LORA_DIO0 -1 // SX1262 does not use DIO0
#define LORA_RESET 2 // Reset pin
#define LORA_DIO1 17 // Interrupt pin (GPIO input)
#define LORA_DIO2 4  // BUSY pin
#define LORA_DIO3  // DIO3 on SX1262 is used as TCXO voltage output, no MCU connection needed; this macro may not actually be used
#define LORA_EN 11 // LoRa module power enable pin

// SPI interface configuration (using SPI2_HOST)
#define LORA_SCK 8   // LoRa SPI clock
#define LORA_MISO 10 // LoRa MISO pin
#define LORA_MOSI 7  // LoRa MOSI pin
#define LORA_CS 18   // LoRa chip select pin (NSS)
// #define HW_SPI1_DEVICE                   // If defined, uses SPI1; default is Arduino SPI (SPI2)

// Antenna control pin
#define LORA_CTRL_GPIO                                                                                                           \
    6 // Antenna enable pin, inverted enable for PE4259 RF Switch CTRL; can be controlled or tied to GND for always-on

// SX1262 specific configuration
#define USE_SX1262                   // Selected LoRa module is SX1262
#define SX126X_CS LORA_CS            // SX1262 chip select pin
#define SX126X_DIO1 LORA_DIO1        // SX1262 interrupt pin
#define SX126X_BUSY LORA_DIO2        // SX1262 busy pin
#define SX126X_RESET LORA_RESET      // SX1262 reset pin
#define SX126X_DIO2_AS_RF_SWITCH     // Use DIO2 as RF switch
#define TCXO_OPTIONAL                // When defined, main.c will attempt initialization twice to ensure it succeeds
#define SX126X_DIO3_TCXO_VOLTAGE 1.8 // DIO3 outputs TCXO voltage to power the active crystal oscillator
#define SX126X_RXEN LORA_CTRL_GPIO   // Connect to MCU or tie to GND for always-active
#define SX126X_TXEN RADIOLIB_NC      // Connect DIO2 to TXEN pin for antenna switch control
#define SX126X_POWER_EN LORA_EN      // LoRa module power enable pin

// E22-900M22S module configuration; this module is SX1268/SX1262+PA+LNA
// (No PA limit info found in datasheet, using default value of 0)
// #define NUM_PA_POINTS 22                 // Non-linear amplifier gain points, unused, kept as reference
#define TX_GAIN_LORA 0      // Amplifier gain, 0-15; 0 disables amplifier; final power = power - TX_GAIN_LORA
#define SX126X_MAX_POWER 22 // Maximum antenna output power; default is 22 in SX126xInterface.cpp, defined here for good practice

// L76K GPS module configuration
#define GPS_L76K
#define GPS_RX_PIN 14          // GPS receive pin (connected to GPS TX)
#define GPS_TX_PIN 9           // GPS transmit pin (connected to GPS RX)
#define GPS_BAUDRATE 9600      // L76K default baud rate
#define GPS_THREAD_INTERVAL 50 // GPS thread interval
// #define PIN_GPS_PPS -1                   // GPS PPS pin (not connected)
// #define PIN_GPS_RESET -1                 // GPS reset pin (not connected)
// #define GPS_RESET_MODE LOW               // GPS reset active level
#define PIN_GPS_EN 12      // GPS enable pin
#define GPS_EN_ACTIVE HIGH // GPS enable active level

// Miscellaneous
// #define HAS_32768HZ 1                    // 32.768kHz crystal oscillator
// #define USE_POWERSAVE                    // Enable power saving mode
// #define SLEEP_TIME 120                   // Sleep duration in seconds

// Module enable
// #define CANNED_MESSAGE_MODULE_ENABLE 1   // Canned message module

// Pin assignments have been optimized to avoid conflicts
#endif /* __TOUCH_LCD_2_SX1262_L76K_VARIANT_H__ */

/* Notes on common LoRa modules: e.g. E22-900M22S requires TCXO power at 1.8V
On the SX1262, DIO3 sets the voltage for an external TCXO, if one is present. If one is not present, use TCXO_OPTIONAL to try both
settings.

| Mfr          | Module           | TCXO | RF Switch | Notes                                 |
| ------------ | ---------------- | ---- | --------- | ------------------------------------- |
| Ebyte        | E22-900M22S      | Yes  | Ext       |                                       |
| Ebyte        | E22-900MM22S     | No   | Ext       |                                       |
| Ebyte        | E22-900M30S      | Yes  | Ext       |                                       |
| Ebyte        | E22-900M33S      | Yes  | Ext       | MAX_POWER must be set to 8 for this   |
| Ebyte        | E220-900M22S     | No   | Ext       | LLCC68, looks like DIO3 not connected |
| AI-Thinker   | RA-01SH          | No   | Int       | SX1262                                |
| Heltec       | HT-RA62          | Yes  | Int       |                                       |
| NiceRF       | Lora1262         | yes  | Int       |                                       |
| Waveshare    | Core1262-HF      | yes  | Ext       |                                       |
| Waveshare    | LoRa Node Module | yes  | Int       |                                       |
| Seeed        | Wio-SX1262       | yes  | Ext       | Cute! DIO2/TXEN are not exposed       |
| AI-Thinker   | RA-02            | No   | Int       | SX1278 **433mhz band only**           |
| RF Solutions | RFM95            | No   | Int       | Untested                              |
| Ebyte        | E80-900M2213S    | Yes  | Int       | LR1121 radio                          |

*/
