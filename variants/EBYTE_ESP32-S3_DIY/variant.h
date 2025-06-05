// Supporting information: https://github.com/S5NC/EBYTE_ESP32-S3/

// Originally developed for E22-900M30S with ESP32-S3-WROOM-1-N4
// NOTE: Uses ESP32-S3-WROOM-1-N4.json in boards folder (via platformio.ini board field), assumes 4 MB (quad SPI) flash, no PSRAM

// FIXME: implement SX12 module type autodetection and have setup for each case (add E32 support)
// E32 has same pinout except having extra pins. I assume that the GND on it is connected internally to other GNDs so it is not a
// problem to NC the extra GND pins.

// For each EBYTE module pin in this section, provide the pin number of the ESP32-S3 you connected it to
// The ESP32-S3 is great because YOU CAN USE PRACTICALLY ANY PINS for the connections, but avoid some pins (such as on the WROOM
// modules the following): strapping pins (except 0 as a user button input as it already has a pulldown resistor in typical
// application schematic) (0, 3, 45, 46), USB-reserved (19, 20), and pins which aren't present on the WROOM-2 module for
// compatiblity as it uses octal SPI, or are likely connected internally in either WROOM version (26-37), and avoid pins whose
// voltages are set by the SPI voltage (47, 48), and pins that don't exist (22-25) You can ALSO set the SPI pins (SX126X_CS,
// SX126X_SCK, SX126X_MISO, SX126X_MOSI) to any pin with the ESP32-S3 due to \ GPIO Matrix / IO MUX / RTC IO MUX \, and also the
// serial pins, but this isn't recommended for Serial0 as the WROOM modules have a 499 Ohm resistor on U0TXD (to reduce harmonics
// but also acting as a sort of protection)

// We have many free pins on the ESP32-S3-WROOM-X-Y module, perhaps it is best to use one of its pins to control TXEN, and use
// DIO2 as an extra interrupt, but right now Meshtastic does not benefit from having another interrupt pin available.

// Adding two 0-ohm links on your PCB design so that you can choose between the two modes for controlling the E22's TXEN would
// enable future software to make the most of an extra available interrupt pin

// Possible improvement: can add extremely low resistance MOSFET to physically toggle power to E22 module when in full sleep (not
// waiting for interrupt)?

// PA stands for Power Amplifier, used when transmitting to increase output power
// LNA stands for Low Noise Amplifier, used when \ listening for / receiving \ data to increase sensitivity

//////////////////////////////////////////////////////////////////////////////////
//                                                                              //
//   Have custom connections or functionality? Configure them in this section   //
//                                                                              //
//////////////////////////////////////////////////////////////////////////////////

//#define USE_SX1262 // E22-900M30S, E22-900M22S, and E22-900MM22S (not E220!) use SX1262
#define USE_SX1268 // E22-400M30S, E22-400M33S, E22-400M22S, and E22-400MM22S use SX1268
#define SX126X_DIO3_TCXO_VOLTAGE 2.2
#define TCXO_OPTIONAL
#define SX126X_MAX_POWER 22 // SX126xInterface.cpp defaults to 22 if not defined, but here we define it for good practice

#define SX126X_CS 4    // EBYTE module's NSS pin
#define SX126X_SCK 5    // EBYTE module's SCK pin
#define SX126X_MOSI 6 // EBYTE module's MOSI pin
#define SX126X_MISO 7  // EBYTE module's MISO pin
#define SX126X_RESET 15 // EBYTE module's NRST pin
#define SX126X_BUSY 16  // EBYTE module's BUSY pin
#define SX126X_DIO1 17  // EBYTE module's DIO1 pin
#define SX126X_DIO2 13  // EBYTE module's DIO2 pin
#define SX126X_TXEN 21
#define SX126X_RXEN 14

#define LORA_CS SX126X_CS     // Compatibility with variant file configuration structure
#define LORA_SCK SX126X_SCK   // Compatibility with variant file configuration structure
#define LORA_MOSI SX126X_MOSI // Compatibility with variant file configuration structure
#define LORA_MISO SX126X_MISO // Compatibility with variant file configuration structure
#define LORA_DIO1 SX126X_DIO1 // Compatibility with variant file configuration structure
#define LORA_DIO2 SX126X_DIO2 // Compatibility with variant file configuration structure
#define E22_TXEN SX126X_TXEN
#define E22_RXEN SX126X_RXEN

// Buttons
#define BUTTON_PIN 18 // Use the BOOT button as the user button

// UART
#define UART_TX 43
#define UART_RX 44

//SCREEN
#define HAS_SCREEN 1 // Assume no screen present by default to prevent crash...
#define USE_SSD1306
#define I2C_SCL 9
#define I2C_SDA 10

#define USE_GPS_E108GN03D
//#define USE_GPS_E108GN04D
#define HAS_GPS 1 // Don't need to set this to 0 to prevent a crash as it doesn't crash if GPS not found, will probe by default
#define PIN_GPS_EN 42
#define GPS_EN_ACTIVE 1
#define GPS_TX_PIN 39
#define GPS_RX_PIN 40

#ifdef USE_GPS_E108GN03D
    #define GPS_BAUDRATE 9600      //E108-GN03D
#elif defined(USE_GPS_E108GN04D)
    #define GPS_BAUDRATE 38400   //E108-GN04D
#endif

// ratio of voltage divider = 3.33 (R1=100k, R2=220k)
#define ADC_MULTIPLIER 3.33
#define BATTERY_PIN 1 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define ADC_CHANNEL ADC1_GPIO1_CHANNEL
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#define ADC_ATTEN ADC_ATTEN_DB_11

// LED
#define LED_PIN 11
#define EXT_NOTIFY_OUT LED_PIN
#define LED_STATE_ON 0 // State when LED is litted

// Buzzer
#define PIN_BUZZER 12





