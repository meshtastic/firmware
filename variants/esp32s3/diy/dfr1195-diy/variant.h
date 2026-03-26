
#define ST7735S 1 // there are different (sub-)versions of ST7735
#define ST7735_CS 17      // LCD_CS
#define ST7735_RS 14      // LCD_DC
#define ST7735_SDA 11     // LCD_MO 
#define ST7735_SCK 12     // LCD_SCK
#define ST7735_RESET 15   // LCD_RST
#define ST7735_MISO 13 //-1
#define ST7735_BUSY -1
#define TFT_BL 16         // LCD_BL
#define ST7735_SPI_HOST SPI3_HOST
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define SCREEN_ROTATE
#define TFT_HEIGHT 160
#define TFT_WIDTH 80
#define TFT_OFFSET_X -24
#define TFT_OFFSET_Y 0
#define TFT_INVERT false
#define FORCE_LOW_RES 1
#define SCREEN_TRANSITION_FRAMERATE 5 // fps
#define DISPLAY_FORCE_SMALL_FONTS
#define TFT_BACKLIGHT_ON LOW
#define USE_TFTDISPLAY 1
//#define DISPLAY_FLIP_SCREEN // doesnt seem to do anything
#define TFT_OFFSET_ROTATION 2

#define VEXT_ENABLE 48 // display
#define VEXT_ON_VALUE LOW

// 
#define SX126X_CS 10    //
#define LORA_SCK 7      //
#define LORA_MOSI 6     //
#define LORA_MISO 5     // 
#define SX126X_RESET 41 // 
#define SX126X_BUSY 40  // 
#define SX126X_DIO1 4   // 

#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_TXEN RADIOLIB_NC
#define SX126X_RXEN 42

#define LED_POWER 21    // If defined we will blink this LED

#define BUTTON_PIN 0 // Use the BOOT button as the user button
#define ALT_BUTTON_PIN 18

#define I2C_SDA 8
#define I2C_SCL 9

#define UART_TX 43
#define UART_RX 44

#define SX126X_MAX_POWER 22

#define HAS_SCREEN 1

#define USE_SX1262

#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define LORA_CS SX126X_CS

#define LORA_DIO1 SX126X_DIO1

#define BATTERY_PIN 1
#define ADC_CHANNEL ADC1_GPIO1_CHANNEL
#define ADC_MULTIPLIER 1.00
#define ADC_ATTENUATION ADC_ATTEN_DB_11