
// TODO: Display (E-Ink)
#define PIN_EINK_EN 11 // BL
#define PIN_EINK_CS 11
#define PIN_EINK_BUSY -1
#define PIN_EINK_DC 21
#define PIN_EINK_RES -1
#define PIN_EINK_SCLK 14
#define PIN_EINK_MOSI 13 // SDI

#define EPD_WIDTH 960
#define EPD_HEIGHT 540

#define I2C_SDA SDA
#define I2C_SCL SCL

#define HAS_TOUCHSCREEN 1
#define GT911_PIN_SDA 39
#define GT911_PIN_SCL 40
#define GT911_PIN_INT 15
#define GT911_PIN_RST 41

#define PCF85063_RTC 0x51
#define HAS_RTC 1

#define USE_POWERSAVE
#define SLEEP_TIME 120

// optional GPS
#define GPS_DEFAULT_NOT_PRESENT 1
#define GPS_RX_PIN 44
#define GPS_TX_PIN 43

#define BUTTON_PIN 48
#define BUTTON_PIN_SECONDARY 0

// SD card
#define HAS_SDCARD
#define SDCARD_USE_SPI1
#define SDCARD_CS SPI_CS
#define SD_SPI_FREQUENCY 75000000U

// battery charger BQ25896
#define HAS_PPM 1
#define XPOWERS_CHIP_BQ25896

// battery quality management BQ27220
#define HAS_BQ27220 1
#define BQ27220_I2C_SDA SDA
#define BQ27220_I2C_SCL SCL
#define BQ27220_DESIGN_CAPACITY 1500

// TPS651851

// LoRa
#define USE_SX1262
#define USE_SX1268

#define LORA_SCK 18
#define LORA_MISO 8
#define LORA_MOSI 17
#define LORA_CS 46

#define LORA_DIO0 -1
#define LORA_RESET 1
#define LORA_DIO1 10 // SX1262 IRQ
#define LORA_DIO2 47 // SX1262 BUSY
#define LORA_DIO3

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 2.4
