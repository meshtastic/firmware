
// Display (E-Ink) ED047TC1 - 8bit parallel
#define USE_EPD
#define EPD_WIDTH 960
#define EPD_HEIGHT 540

#if defined(T5_S3_EPAPER_PRO_V1)
#define BOARD_BL_EN 40
#else
#define BOARD_BL_EN 11
#endif

#define I2C_SDA SDA
#define I2C_SCL SCL

#define HAS_TOUCHSCREEN 1
#define GT911_PIN_SDA SDA
#define GT911_PIN_SCL SCL
#if defined(T5_S3_EPAPER_PRO_V1)
#define GT911_PIN_INT 15
#define GT911_PIN_RST 41
#else
#define GT911_PIN_INT 3
#define GT911_PIN_RST 9
#endif

#define PCF85063_RTC 0x51
#define HAS_RTC 1
#define PCF85063_INT 2

#define USE_POWERSAVE
#define SLEEP_TIME 120

// GPS
#if !defined(T5_S3_EPAPER_PRO_V1)
#define GPS_RX_PIN 44
#define GPS_TX_PIN 43
#endif

#define BUTTON_PIN 0
#define PIN_BUTTON2 48
#define ALT_BUTTON_PIN PIN_BUTTON2

// SD card
#define HAS_SDCARD
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

#if !defined(T5_S3_EPAPER_PRO_V1)
// TPS651851

// PCA9535 IO extender
#define USE_XL9555
#define PCA9535_ADDR 0x20
#define PCA9535_INT 38
#define PCA9535_IO00_LORA_EN 00
#define PCA9535_IO10_EP_OE 10   // EP Output enable source driver
#define PCA9535_IO11_EP_MODE 11 // EP Output mode selection gate driver
#define PCA9535_IO12_BUTTON 12
#define PCA9535_IO13_TPS_PWRUP 13
#define PCA9535_IO14_VCOM_CTRL 14
#define PCA9535_IO15_TPS_WAKEUP 15
#define PCA9535_IO16_TPS_PWR_GOOD 16
#define PCA9535_IO17_TPS_INT 17
#endif

// LoRa
#define USE_SX1262
#define USE_SX1268

#define LORA_SCK SCK
#define LORA_MISO MISO
#define LORA_MOSI MOSI
#define LORA_CS 46

#define LORA_DIO0 -1
#if defined(T5_S3_EPAPER_PRO_V1)
#define LORA_RESET 43
#define LORA_DIO1 3  // SX1262 IRQ
#define LORA_DIO2 44 // SX1262 BUSY
#define LORA_DIO3
#else
#define LORA_RESET 1
#define LORA_DIO1 10 // SX1262 IRQ
#define LORA_DIO2 47 // SX1262 BUSY
#define LORA_DIO3
#endif

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 2.4
