// Display (E-Ink)
#define PIN_EINK_CS 34
#define PIN_EINK_BUSY 37
#define PIN_EINK_DC 35
#define PIN_EINK_RES -1
#define PIN_EINK_SCLK 36
#define PIN_EINK_MOSI 47

#define I2C_SDA SDA
#define I2C_SCL SCL

// CST328 touch screen (implementation in src/platform/extra_variants/t_deck_pro/variant.cpp)
#define HAS_TOUCHSCREEN 1
#define CST328_PIN_INT 12
#define CST328_PIN_RST 45

#define USE_POWERSAVE
#define SLEEP_TIME 120

// GNNS
#define HAS_GPS 1
#define GPS_BAUDRATE 38400
#define PIN_GPS_EN 15
#define GPS_EN_ACTIVE 1
#define GPS_RX_PIN 44
#define GPS_TX_PIN 43
#define PIN_GPS_PPS 1

#define BUTTON_PIN 0

// vibration motor
#define PIN_VIBRATION 2

// Have SPI interface SD card slot
#define HAS_SDCARD
#define SDCARD_USE_SPI1
#define SPI_MOSI (33)
#define SPI_SCK (36)
#define SPI_MISO (47)
#define SPI_CS (48)
#define SDCARD_CS SPI_CS
#define SD_SPI_FREQUENCY 75000000U

// TCA8418 keyboard
#define KB_BL_PIN 42
#define CANNED_MESSAGE_MODULE_ENABLE 1

// microphone PCM5102A
#define PCM5102A_SCK 47
#define PCM5102A_DIN 17
#define PCM5102A_LRCK 18

// LTR_553ALS light sensor
#define HAS_LTR553ALS

// gyroscope BHI260AP
#define BOARD_1V8_EN 38
#define HAS_BHI260AP

// battery charger BQ25896
#define HAS_PPM 1
#define XPOWERS_CHIP_BQ25896

// battery quality management BQ27220
#define HAS_BQ27220 1
#define BQ27220_I2C_SDA SDA
#define BQ27220_I2C_SCL SCL
#define BQ27220_DESIGN_CAPACITY 1400

// LoRa
#define USE_SX1262
#define USE_SX1268

#define LORA_EN 46 // LoRa enable pin
#define LORA_SCK 36
#define LORA_MISO 47
#define LORA_MOSI 33
#define LORA_CS 3

#define LORA_DIO0 -1 // a No connect on the SX1262 module
#define LORA_RESET 4
#define LORA_DIO1 5 // SX1262 IRQ
#define LORA_DIO2 6 // SX1262 BUSY
#define LORA_DIO3   // Not connected on PCB, but internally on the TTGO SX1262, if DIO3 is high the TXCO is enabled

#define SX126X_CS LORA_CS // FIXME - we really should define LORA_CS instead
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
// Not really an E22 but TTGO seems to be trying to clone that
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 2.4
// Internally the TTGO module hooks the SX1262-DIO2 in to control the TX/RX switch (which is the default for the sx1262interface
// code)
