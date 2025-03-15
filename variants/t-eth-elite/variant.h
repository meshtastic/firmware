#define HAS_SDCARD
#define SDCARD_USE_SPI1

#define HAS_GPS 1
#define GPS_RX_PIN 39
#define GPS_TX_PIN 42
#define GPS_BAUDRATE_FIXED 1
#define GPS_BAUDRATE 9600

#define I2C_SDA 17 // I2C pins for this board
#define I2C_SCL 18

#define HAS_SCREEN 1 // Allow for OLED Screens on I2C Header of shield

#define LED_PIN 38   // If defined we will blink this LED
#define BUTTON_PIN 0 // If defined, this will be used for user button presses,

#define BUTTON_NEED_PULLUP

// TTGO uses a common pinout for their SX1262 vs RF95 modules - both can be enabled and we will probe at runtime for RF95 and if
// not found then probe for SX1262
#define USE_RF95 // RFM95/SX127x
#define USE_SX1262
#define USE_SX1280
#define USE_LR1121

#define LORA_SCK 10
#define LORA_MISO 9
#define LORA_MOSI 11
#define LORA_CS 40
#define LORA_RESET 46

// per SX1276_Receive_Interrupt/utilities.h
#define LORA_DIO0 8
#define LORA_DIO1 16
#define LORA_DIO2 RADIOLIB_NC

// per SX1262_Receive_Interrupt/utilities.h
#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 8
#define SX126X_BUSY 16
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif

// per SX128x_Receive_Interrupt/utilities.h
#ifdef USE_SX1280
#define SX128X_CS LORA_CS
#define SX128X_DIO1 8
#define SX128X_DIO2 33
#define SX128X_DIO3 34
#define SX128X_BUSY 16
#define SX128X_RESET LORA_RESET
#define SX128X_RXEN 13
#define SX128X_TXEN 38
#define SX128X_MAX_POWER 3
#endif

// LR1121
#ifdef USE_LR1121
#define LR1121_IRQ_PIN 8
#define LR1121_NRESET_PIN LORA_RESET
#define LR1121_BUSY_PIN 16
#define LR1121_SPI_NSS_PIN LORA_CS
#define LR1121_SPI_SCK_PIN LORA_SCK
#define LR1121_SPI_MOSI_PIN LORA_MOSI
#define LR1121_SPI_MISO_PIN LORA_MISO
#define LR11X0_DIO3_TCXO_VOLTAGE 3.0
#define LR11X0_DIO_AS_RF_SWITCH
#endif

#define HAS_ETHERNET 1
#define USE_WS5500 1 // this driver uses the same stack as the ESP32 Wifi driver

#define ETH_MISO_PIN 47
#define ETH_MOSI_PIN 21
#define ETH_SCLK_PIN 48
#define ETH_CS_PIN 45
#define ETH_INT_PIN 14
#define ETH_RST_PIN -1
#define ETH_ADDR 1