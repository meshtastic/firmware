// ST7796 TFT LCD
#define TFT_CS 38
#define ST7796_CS TFT_CS
#define ST7796_RS 37    // DC
#define ST7796_SDA MOSI // MOSI
#define ST7796_SCK SCK
#define ST7796_RESET -1
#define ST7796_MISO MISO
#define ST7796_BUSY -1
#define ST7796_BL 42
#define ST7796_SPI_HOST SPI2_HOST
#define TFT_BL 42
#define SPI_FREQUENCY 75000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 480
#define TFT_WIDTH 222
#define TFT_OFFSET_X 49
#define TFT_OFFSET_Y 0
#define TFT_OFFSET_ROTATION 3
#define SCREEN_ROTATE
#define SCREEN_TRANSITION_FRAMERATE 5
#define BRIGHTNESS_DEFAULT 130 // Medium Low Brightness

#define I2C_SDA SDA
#define I2C_SCL SCL

#define USE_POWERSAVE
#define SLEEP_TIME 120

// GNNS
#define HAS_GPS 1
#define GPS_BAUDRATE 38400
#define GPS_RX_PIN 4
#define GPS_TX_PIN 12
#define PIN_GPS_PPS 13

// PCF8563 RTC Module
#if __has_include("pcf8563.h")
#include "pcf8563.h"
#endif
#define PCF8563_RTC 0x51
#define HAS_RTC 1

// Rotary
#define ROTARY_A (40)
#define ROTARY_B (41)
#define ROTARY_PRESS (7)

#define BUTTON_PIN 0

// SPI interface SD card slot
#define SPI_MOSI MOSI
#define SPI_SCK SCK
#define SPI_MISO MISO
#define SPI_CS 21
#define SDCARD_CS SPI_CS
#define SD_SPI_FREQUENCY 75000000U

// TCA8418 keyboard
#define I2C_NO_RESCAN
#define KB_BL_PIN 46
#define KB_INT 6
#define CANNED_MESSAGE_MODULE_ENABLE 1

// audio codec ES8311
#define HAS_I2S
#define DAC_I2S_BCK 11
#define DAC_I2S_WS 18
#define DAC_I2S_DOUT 45
#define DAC_I2S_DIN 17
#define DAC_I2S_MCLK 10

// gyroscope BHI260AP
#define HAS_BHI260AP

// battery charger BQ25896
#define HAS_PPM 1
#define XPOWERS_CHIP_BQ25896

// battery quality management BQ27220
#define HAS_BQ27220 1
#define BQ27220_I2C_SDA SDA
#define BQ27220_I2C_SCL SCL
#define BQ27220_DESIGN_CAPACITY 1500

// NFC ST25R3916
#define NFC_INT 5
#define NFC_CS 39

// External expansion chip XL9555
#define USE_XL9555
#define EXPANDS_DRV_EN (0)
#define EXPANDS_AMP_EN (1)
#define EXPANDS_KB_RST (2)
#define EXPANDS_LORA_EN (3)
#define EXPANDS_GPS_EN (4)
#define EXPANDS_NFC_EN (5)
#define EXPANDS_GPS_RST (7)
#define EXPANDS_KB_EN (8)
#define EXPANDS_GPIO_EN (9)
#define EXPANDS_SD_DET (10)
#define EXPANDS_SD_PULLEN (11)
#define EXPANDS_SD_EN (12)

// LoRa
#define USE_SX1262
#define USE_SX1268

#define LORA_SCK 35
#define LORA_MISO 33
#define LORA_MOSI 34
#define LORA_CS 36

#define LORA_DIO0 -1 // a No connect on the SX1262 module
#define LORA_RESET 47
#define LORA_DIO1 14 // SX1262 IRQ
#define LORA_DIO2 48 // SX1262 BUSY
#define LORA_DIO3    // Not connected on PCB, but internally on the TTGO SX1262, if DIO3 is high the TXCO is enabled

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 3.0
