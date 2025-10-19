
// Display (E-Ink) ED047TC1
#define USE_EPD
#define BOARD_BL_EN 11
#define EP_I2C_PORT I2C_NUM_0
#define EP_SCL (40)
#define EP_SDA (39)
#define EP_INTR (38)
#define EP_D7 (8)
#define EP_D6 (18)
#define EP_D5 (17)
#define EP_D4 (16)
#define EP_D3 (15)
#define EP_D2 (7)
#define EP_D1 (6)
#define EP_D0 (5)
#define EP_CKV (48)
#define EP_STH (41)
#define EP_LEH (42)
#define EP_STV (45)
#define EP_CKH (4)

#define EPD_WIDTH 960
#define EPD_HEIGHT 540

#define I2C_SDA SDA
#define I2C_SCL SCL

#define HAS_TOUCHSCREEN 1
#define GT911_PIN_SDA 39
#define GT911_PIN_SCL 40
#define GT911_PIN_INT 3
#define GT911_PIN_RST 9

#define PCF85063_RTC 0x51
#define HAS_RTC 1
#define PCF85063_INT 2

#define USE_POWERSAVE
#define SLEEP_TIME 120

// GPS
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

// LoRa
#define USE_SX1262
#define USE_SX1268

#define LORA_SCK 14  // 18
#define LORA_MISO 21 // 8
#define LORA_MOSI 13 // 17
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

/* V1

#define BOARD_SCL         (5)
#define BOARD_SDA         (6)

#define BOARD_SPI_MISO    (8)
#define BOARD_SPI_MOSI    (17)
#define BOARD_SPI_SCLK    (18)

#define BOARD_SD_MISO    (BOARD_SPI_MISO)
#define BOARD_SD_MOSI    (BOARD_SPI_MOSI)
#define BOARD_SD_SCLK    (BOARD_SPI_SCLK)
#define BOARD_SD_CS      (16)

#define BOARD_LORA_MISO   (BOARD_SPI_MISO)
#define BOARD_LORA_MOSI   (BOARD_SPI_MOSI)
#define BOARD_LORA_SCLK   (BOARD_SPI_SCLK)
#define BOARD_LORA_CS     (46)
#define BOARD_LORA_IRQ    (3)
#define BOARD_LORA_RST    (43)
#define BOARD_LORA_BUSY   (44)

#define BOARD_TOUCH_SCL   (BOARD_SCL)
#define BOARD_TOUCH_SDA   (BOARD_SDA)
#define BOARD_TOUCH_INT   (15)
#define BOARD_TOUCH_RST   (41)

#define BOARD_RTC_INT     7
#define BOARD_RT_SCL      (BOARD_SCL)
#define BOARD_RT_SDA      (BOARD_SDA)

#define BOARD_BL_EN       (40)
#define BOARD_BATT_PIN    (4)
#define BOARD_BOOT_BTN    (0)
#define BOARD_KEY_BTN     (48)

*/