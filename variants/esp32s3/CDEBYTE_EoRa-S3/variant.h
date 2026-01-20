// LED - status indication
#define LED_PIN 37

// Button - user interface
#define BUTTON_PIN 0 // This is the BOOT button, and it has its own pull-up resistor

// SD card - TODO: test, currently untested, copied from T3S3 variant
#define HAS_SDCARD
#define SDCARD_USE_SPI1
// TODO: rename this to make this SD-card specific
#define SPI_CS 13
#define SPI_SCK 14
#define SPI_MOSI 11
#define SPI_MISO 2
// FIXME: there are two other SPI pins that are not defined here
// Compatibility
#define SDCARD_CS SPI_CS

// Battery voltage monitoring - TODO: test, currently untested, copied from T3S3 variant
#define BATTERY_PIN 1 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define ADC_CHANNEL ADC1_GPIO1_CHANNEL
#define ADC_MULTIPLIER                                                                                                           \
    2.11 // ratio of voltage divider = 2.0 (R10=1M, R13=1M), plus some undervoltage correction - TODO: this was carried over from
         // the T3S3, test to see if the undervoltage correction is needed.

// Display - OLED connected via I2C by the default hardware configuration
#define HAS_SCREEN 1
#define USE_SSD1306
#define I2C_SCL 17
#define I2C_SDA 18

// UART - The 1mm JST SH connector closest to the USB-C port
#define UART_TX 43
#define UART_RX 44

// Peripheral I2C - The 1mm JST SH connector furthest from the USB-C port which follows Adafruit connection standard. There are no
// pull-up resistors on these lines, the downstream device needs to include them. TODO: test, currently untested
#define I2C_SCL1 21
#define I2C_SDA1 10

// Radio
#define USE_SX1262 // CDEBYTE EoRa-S3-900TB <- CDEBYTE E22-900MM22S <- Semtech SX1262
#define USE_SX1268 // CDEBYTE EoRa-S3-400TB <- CDEBYTE E22-400MM22S <- Semtech SX1268

#define SX126X_CS 7
#define LORA_SCK 5
#define LORA_MOSI 6
#define LORA_MISO 3
#define SX126X_RESET 8
#define SX126X_BUSY 34
#define SX126X_DIO1 33

#define SX126X_DIO2_AS_RF_SWITCH // All switching is performed with DIO2, it is automatically inverted using circuitry.
// CDEBYTE EoRa-S3 uses an XTAL, thus we do not need DIO3 as TCXO voltage reference. Don't define SX126X_DIO3_TCXO_VOLTAGE for
// simplicity rather than defining it as 0.
#define SX126X_MAX_POWER                                                                                                         \
    22 // E22-900MM22S and E22-400MM22S have a raw SX1262 or SX1268 respsectively, they are rated to output up and including 22
       // dBm out of their SX126x IC.

// Compatibility with old variant.h file structure - FIXME: this should be done in the respective radio interface modules to clean
// up all variants.
#define LORA_CS SX126X_CS
#define LORA_DIO1 SX126X_DIO1