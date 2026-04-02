#include <stdint.h>

// DFROBOT  DFR1195

#define DFRobot_LoRaWAN_ESP32_S3

#define LED_POWER 21
#define BUTTON_PIN 18

// 屏幕
#define TFT_DC 14
#define TFT_CS 17
#define TFT_RST 15
// #define TFT_BL 16       // Backlight pin

// ST7735S TFT LCD
#define USE_TFTDISPLAY 1
#define ST7735S 1 // there are different (sub-)versions of ST7735
#define ST7735_CS TFT_CS
#define ST7735_RS TFT_DC  // DC
#define ST7735_SDA MOSI // MOSI
#define ST7735_SCK SCK
#define ST7735_RESET TFT_RST
#define ST7735_MISO -1
#define ST7735_BUSY -1
#define TFT_BL 16 /* V1.1 PCB marking */
#define ST7735_SPI_HOST SPI3_HOST
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 160
#define TFT_WIDTH 80
#define TFT_OFFSET_X -24
#define TFT_OFFSET_Y 0
#define SCREEN_ROTATE
#define SCREEN_TRANSITION_FRAMERATE 3 // fps
#define DISPLAY_FORCE_SMALL_FONTS
#define FORCE_LOW_RES 1
#define VEXT_ENABLE 48      // LCD power enable pin
#define VEXT_ON_VALUE LOW


#define LORA_ANTPWR  42     //RXEN
#define LORA_RST     41     //RST
#define LORA_BUSY    40     //BUSY
#define LORA_DIO1    4      //DIO

#define LORA_CS     10
#define LORA_MOSI   6
#define LORA_MISO   5
#define LORA_SCK    7

#define USE_SX1262
#define SX126X_CS LORA_CS                
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RST
#define SX126X_POWER_EN LORA_ANTPWR
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 3.3