//////////////////////////////////////////////////////////////////////////////////
//                                                                              //
//   Have custom connections or functionality? Configure them in this section   //
//                                                                              //
//////////////////////////////////////////////////////////////////////////////////

// Debugging
// #define GPS_DEBUG

// Lora
#define USE_LLCC68 // Original Chatter2 with LLCC68 module
#define USE_SX1262 // Added for when Lora module is swapped for HT-RA62

#define SX126X_CS 14                 // module's NSS pin
#define LORA_SCK 16                  // module's SCK pin
#define LORA_MOSI 5                  // module's MOSI pin
#define LORA_MISO 17                 // module's MISO pin
#define SX126X_RESET RADIOLIB_NC     // module's NRST pin
#define SX126X_BUSY 4                // module's BUSY pin works for both LLCC68 and RA-62 with cut & jumper
#define SX126X_DIO1 18               // module's DIO1 pin
#define SX126X_DIO2_AS_RF_SWITCH     // module's DIO2 pin
#define SX126X_DIO3_TCXO_VOLTAGE 1.8 // module's DIO pin
#define SX126X_TXEN RADIOLIB_NC
#define SX126X_RXEN RADIOLIB_NC

// Status
// #define LED_PIN 1
// External notification
// FIXME: Check if EXT_NOTIFY_OUT actualy has any effect and removes the need for setting the external notication pin in the
// app/preferences
// #define EXT_NOTIFY_OUT 2 // The GPIO pin that acts as the external notification output (here we connect an LED to it)

// Buzzer
#define PIN_BUZZER 19
// Buttons
// #define BUTTON_PIN 36 // Use the WAKE button as the user button
// I2C
// #define I2C_SCL 27
// #define I2C_SDA 26

#define SX126X_MAX_POWER 22 // SX126xInterface.cpp defaults to 22 if not defined, but here we define it for good practice

// Display

#define HAS_SCREEN 1 // Assume no screen present by default to prevent crash...

// ST7735S TFT LCD
#define ST7735S 1 // there are different (sub-)versions of ST7735
#define ST7735_CS -1
#define ST7735_RS 33  // DC
#define ST7735_SDA 26 // MOSI
#define ST7735_SCK 27
#define ST7735_RESET 15
#define ST7735_MISO -1
#define ST7735_BUSY -1
#define TFT_BL 32
#define ST7735_SPI_HOST HSPI_HOST // SPI2_HOST for S3, auto may work too
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 160
#define TFT_WIDTH 128
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define TFT_INVERT false
#define SCREEN_ROTATE
#define SCREEN_TRANSITION_FRAMERATE 5 // fps
#define DISPLAY_FORCE_SMALL_FONTS
#define TFT_BACKLIGHT_ON LOW

// Battery

#define BATTERY_PIN 34 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define ADC_CHANNEL ADC1_GPIO34_CHANNEL
#define ADC_ATTENUATION                                                                                                          \
    ADC_ATTEN_DB_2_5       // 2_5-> 100mv-1250mv, 11-> 150mv-3100mv for ESP32
                           // ESP32-S2/C3/S3 are different
                           // lower dB for lower voltage rnage
#define ADC_MULTIPLIER 5.0 // VBATT---10k--pin34---2.5K---GND
// Chatter2 uses 3 AAA cells
#define CELL_TYPE_ALKALINE
#define NUM_CELLS 3
#undef EXT_PWR_DETECT

// GPS
// FIXME: unsure what to define HAS_GPS as if GPS isn't always present
#define HAS_GPS 1 // Don't need to set this to 0 to prevent a crash as it doesn't crash if GPS not found, will probe by default
// #define PIN_GPS_EN 15
// #define GPS_EN_ACTIVE 1
#undef GPS_TX_PIN
#undef GPS_RX_PIN
#define GPS_TX_PIN 13
#define GPS_RX_PIN 2

// keyboard
#define INPUTBROKER_SERIAL_TYPE 1
#define KB_LOAD 21 // load values from the switch and store in shift register
#define KB_CLK 22  // clock pin for serial data out
#define KB_DATA 23 // data pin
#define CANNED_MESSAGE_MODULE_ENABLE 1

/////////////////////////////////////////////////////////////////////////////////
//                                                                             //
//   You should have no need to modify the code below, nor in pins_arduino.h   //
//                                                                             //
/////////////////////////////////////////////////////////////////////////////////

#define LORA_CS SX126X_CS // FIXME: for some reason both are used in /src

// Many of the below values would only be used if USE_RF95 was defined, but it's not as we aren't actually using an RF95, just
// that the 4 pins above are named like it If they aren't used they don't need to be defined and doing so cause confusion to those
// adapting this file LORA_RESET value is never used in src (as we are not using RF95), so no need to define LORA_DIO0 is not used
// in src (as we are not using RF95) as SX1262 does not have it per SX1262 datasheet, so no need to define
// FIXME: confirm that the linked lines below are actually only called when using the SX126x or SX128x and no other modules
// then use SX126X_DIO1 and SX128X_DIO1 respectively for that purpose, removing the need for RF95-style LORA_* definitions when
// the RF95 isn't used
#define LORA_DIO1                                                                                                                \
    SX126X_DIO1 // The old name is used in
                // https://github.com/meshtastic/firmware/blob/7eff5e7bcb2084499b723c5e3846c15ee089e36d/src/sleep.cpp#L298, so
                // must also define the old name
// LORA_DIO2 value is never used in src (as we are not using RF95), so no need to define, and if DIO2_AS_RF_SWITCH is set then it
// cannot serve any extra function even if requested to LORA_DIO3 value is never used in src (as we are not using RF95), so no
// need to define, and DIO3_AS_TCXO_AT_1V8 is set so it cannot serve any extra function even if requested to (from 13.3.2.1
// DioxMask in SX1262 datasheet: Note that if DIO2 or DIO3 are used to control the RF Switch or the TCXO, the IRQ will not be
// generated even if it is mapped to the pins.)
