// ST7796 TFT LCD
#define TFT_CS 39
#define ST7796_CS TFT_CS
#define ST7796_RS 9     // DC
#define ST7796_SDA MOSI // MOSI
#define ST7796_SCK SCK
#define ST7796_RESET -1
#define ST7796_MISO MISO
#define ST7796_BUSY -1
#define ST7796_BL 48
#define ST7796_SPI_HOST SPI2_HOST
#define TFT_BL 48
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
#define USE_TFTDISPLAY 1

#define I2C_SDA SDA
#define I2C_SCL SCL

// #define I2C_SDA1
// #define I2C_SCL1

#define USE_POWERSAVE
#define SLEEP_TIME 120

#define PIN_POWER_EN 42

// GNNS
#define GPS_DEFAULT_NOT_PRESENT 1

// PCF85063 RTC Module
#define PCF85063_RTC 0x51

// SY6970 battery charger
#define SY6970_Address 0x6A
#define SY6970_INT 21

// MAX98357A PCM amplifier
#define HAS_I2S
#define DAC_I2S_BCK 4
#define DAC_I2S_WS 15
#define DAC_I2S_DOUT 11
#define DAC_I2S_MCLK 13 // not used
#define MAX98357A_EN 41

// ICM20948 motion tracker
#define ICM20948_ADDRESS 0x28
#define ICM20948_INT 21

// MP34DT05TRF MEMS mic
#define MP34DT05TR_LRCLK 1
#define MP34DT05TR_DATA 2
#define MP34DT05TR_EN 3

#define BUTTON_PIN 16
#define BUTTON_NEED_PULLUP
#define ALT_BUTTON_PIN 12
#define ALT_BUTTON_ACTIVE_LOW true
#define ALT_BUTTON_ACTIVE_PULLUP true
#define PIN_BUTTON3 0

// vibration motor
#define PIN_VIBRATION 45

// SPI interface SD card slot
#define SPI_MOSI MOSI
#define SPI_SCK SCK
#define SPI_MISO MISO
#define SPI_CS 14
#define SDCARD_CS SPI_CS
#define SD_SPI_FREQUENCY 75000000U

// LoRa
// #define USE_SX1262
// #define USE_SX1268
// #define USE_SX1280
#define USE_LR1121

#define LORA_SCK 18
#define LORA_MISO 8
#define LORA_MOSI 17
#define LORA_CS 7
#define LORA_RESET 10

#define LORA_DIO0 -1 // a No connect on the SX1262 module
#define LORA_DIO1 40 // SX1262 IRQ
#define LORA_DIO2 46 // SX1262 BUSY
#define LORA_DIO3    // Not connected on PCB, but internally on the TTGO SX1262, if DIO3 is high the TXCO is enabled

#if defined(USE_SX1262) || defined(USE_SX1268)
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 3.0
#endif

#ifdef USE_SX1280
#define SX128X_CS LORA_CS
#define SX128X_DIO1 LORA_DIO1
#define SX128X_BUSY LORA_DIO2
#define SX128X_RESET LORA_RESET
#endif

#ifdef USE_LR1121
#define LR1121_IRQ_PIN LORA_DIO1
#define LR1121_NRESET_PIN LORA_RESET
#define LR1121_BUSY_PIN LORA_DIO2
#define LR1121_SPI_NSS_PIN LORA_CS
#define LR1121_SPI_SCK_PIN LORA_SCK
#define LR1121_SPI_MOSI_PIN LORA_MOSI
#define LR1121_SPI_MISO_PIN LORA_MISO
#define LR11X0_DIO3_TCXO_VOLTAGE 3.0
#define LR11X0_DIO_AS_RF_SWITCH
#endif
