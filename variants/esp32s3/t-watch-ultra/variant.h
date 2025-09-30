
// CO5300 TFT AMOLED
#define CO5300_CS 41
#define CO5300_SCK 40
#define CO5300_RESET 37
#define CO5300_TE 6
#define CO5300_IO0 38
#define CO5300_IO1 39
#define CO5300_IO2 42
#define CO5300_IO3 45
#define CO5300_SPI_HOST SPI2_HOST
#define SPI_FREQUENCY 75000000
#define SPI_READ_FREQUENCY 16000000 // irrelevant
#define TFT_HEIGHT 502
#define TFT_WIDTH 410
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define TFT_OFFSET_ROTATION 0
#define SCREEN_TRANSITION_FRAMERATE 5 // fps

#define HAS_TOUCHSCREEN 1
#define SCREEN_TOUCH_INT 12
#define TOUCH_I2C_PORT 0
#define TOUCH_SLAVE_ADDRESS 0x1A
#define WAKE_ON_TOUCH

#define BUTTON_PIN 0

#define USE_POWERSAVE
#define SLEEP_TIME 120

// External expansion chip XL9555
#define USE_XL9555

// MAX98357A
#define HAS_I2S
#define DAC_I2S_BCK 9
#define DAC_I2S_WS 10
#define DAC_I2S_DOUT 11
#define DAC_I2S_MCLK 0 // TODO

#define HAS_AXP2101
// #define PMU_IRQ 7
#define HAS_RTC 1
#define HAS_DRV2605 1

#define I2C_SDA 3
#define I2C_SCL 2
#define I2C_NO_RESCAN

#define HAS_GPS 1
#define GPS_BAUDRATE 38400
#define GPS_RX_PIN 44
#define GPS_TX_PIN 43
#define PIN_GPS_PPS 13

// SPI interface SD card slot
#define SPI_MOSI MOSI
#define SPI_SCK SCK
#define SPI_MISO MISO
#define SPI_CS 21
#define SD_SPI_FREQUENCY 75000000U

#define USE_SX1262
// #define USE_SX1280

#define LORA_SCK 35
#define LORA_MISO 33
#define LORA_MOSI 34
#define LORA_CS 36

#define LORA_DIO0 -1 // a No connect on the SX1262 module
#define LORA_RESET 47
#define LORA_DIO1 14 // SX1262 IRQ
#define LORA_DIO2 48 // SX1262 BUSY
#define LORA_DIO3

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define USE_VIRTUAL_KEYBOARD 1
#define DISPLAY_CLOCK_FRAME 1
