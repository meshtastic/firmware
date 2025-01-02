// Compatible with EoRa-S3-900TB (https://www.cdebyte.com/products/EoRa-S3-900TB) (includes E22-900MM22S with SX1262)
// Compatible with EoRa-S3-400TB (https://www.cdebyte.com/products/EoRa-S3-400TB) (includes E22-400MM22S with SX1268)
// Very similar to T3S3 V1.2 (ESP32-S3FH4R2), except uses their own radio IC module, lacks peripheral LDO
// enable/disable control, uses passive oscillator instead of TCXO. 

// LED - status indication
// D5 on schematic
#define LED_PIN 37

// Button - user interface
// U3 on schematic, pulled up in hardware (10 kΩ), and by ESP32-S3 (very weakly) as also served as the ESP32-S3's BOOT button
#define BUTTON_PIN 0

// SD card - Used in SPI mode
// Connected to peripheral 3.3V supply (net +3.3VB)
// TODO: test, currently untested
#define HAS_SDCARD
#define SDCARD_USE_SPI1 // Unsure what this does or if needed
// TODO: rename this where used to make it SD-card SPI-mode specific
#define SPI_CS 13 // net SD_DAT3/CS
#define SPI_SCK 14 // net SD_CLK
#define SPI_MOSI 11 // net SD_CMD/MOSI
#define SPI_MISO 2 // net SD_CAT0/MISO
// SD_DAT1 3 // not used in SPI mode
// SD_DAT2 12 // not used in SPI mode
#define SDCARD_CS SPI_CS // Compatibility with old variant.h file structure - FIXME: clear up via /src

// Battery voltage monitoring
// TODO: test, currently untested, copied from T3S3 variant
// The battery voltage is split in two using R10 and R13, both 1 MΩ, and fed to the ESP32-S3's GPIO1 via the net V_BAT
// This gives a voltage divider of ratio 2.0
// TODO: We carried over the value 2.11 from the T3S3, check if this undervoltage correction is needed
#define BATTERY_PIN 1
#define ADC_CHANNEL ADC1_GPIO1_CHANNEL // because it's connected to GPIO1 - FIXME: should really be done automatically
#define ADC_MULTIPLIER 2.11 // includes the inherited undervoltage compensation mentioned above

// Display - OLED connected via I2C by the default hardware configuration
// Connected to peripheral 3.3V supply (net +3.3VB)
#define HAS_SCREEN 1
#define USE_SSD1306
#define I2C_SCL 17 // net OLED_D0
#define I2C_SDA 18 // net OLED_D1

// UART - The 1mm JST SH connector (J8) closest to the USB-C port
// The connector also provides the same 3.3V supply delivered to the ESP32-S3 (+3.3VA) and global GND
// Direct access to ESP32-S3 pin
#define UART_TX 43
#define UART_RX 44
// Access is also provided to the same UART pins, with their own 22 Ω resistor each via the module's pins 11 (RX) and 12 (TX)

// Peripheral I2C - The 1mm JST SH connector (J7) furthest from the USB-C port which follows Adafruit connection standard.
// There are no pull-up resistors on these lines, the downstream device needs to include them. TODO: test, currently untested
// The connector also provides the same 3.3V supply delivered to the ESP32-S3 (+3.3VA) and global GND
#define I2C_SCL1 21
#define I2C_SDA1 10

// Radios supported, probe both
#define USE_SX1262 // CDEBYTE EoRa-S3-900TB <- CDEBYTE E22-900MM22S <- Semtech SX1262
#define USE_SX1268 // CDEBYTE EoRa-S3-400TB <- CDEBYTE E22-400MM22S <- Semtech SX1268

#define SX126X_CS 7 // net E22_NSS
#define LORA_SCK 5 // net E22_SCK
#define LORA_MOSI 6 // net E22_MOSI
#define LORA_MISO 3 // net E22_MISO
#define SX126X_RESET 8 // net E22_NRST
#define SX126X_BUSY 34 // net E22_BUSY
#define SX126X_DIO1 33 // net E22_DIO1

#define SX126X_DIO2_AS_RF_SWITCH // All switching is performed with DIO2, it is automatically inverted using hardware.
// CDEBYTE EoRa-S3 uses an XTAL, thus we do not need DIO3 as TCXO voltage reference. Don't define SX126X_DIO3_TCXO_VOLTAGE for
// simplicity rather than defining it as 0.
#define SX126X_MAX_POWER 22 // Both boards can output up to and including 22 dBm from their SX126x IC.

// Compatibility with old variant.h file structure - FIXME: clear up via /src
#define LORA_CS SX126X_CS
#define LORA_DIO1 SX126X_DIO1
