// #define BUTTON_NEED_PULLUP // if set we need to turn on the internal CPU pullup during sleep

#define I2C_SDA 21
#define I2C_SCL 22

#define BUTTON_NEED_PULLUP
// #define EXT_NOTIFY_OUT 13 // Default pin to use for Ext Notify Plugin.
// Select Use the middle button, you can also use 39, 37
#define BUTTON_PIN 38

#define PIN_BUZZER 25
// The Module used here is module-LORa433_V1.1. You can also use module-LORa868_V1.1
// https://docs.m5stack.com/en/module/Module-LoRa433_V1.1
// https://docs.m5stack.com/en/module/Module-LoRa868_V1.1
#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS 5   //NSS

#define USE_RF95
#define LORA_DIO0 35  // IRQ 
#define LORA_RESET 13 // RST
#define LORA_DIO1 RADIOLIB_NC // Not really used
#define LORA_DIO2 RADIOLIB_NC // Not really used

// The gnss module is used here     https://docs.m5stack.com/en/module/GNSS%20Module
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17


//Note: If you use corebasic version 2.7 or later, 
//you need to go to the src>graphics>TFTDisplay.cpp file to change the value of cfg.invert, 
//this one is to set the color inversion
#define TFT_HEIGHT 240
#define TFT_WIDTH 320
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define TFT_BUSY -1

// LCD screens are slow, so slowdown the wipe so it looks better
#define SCREEN_TRANSITION_FRAMERATE 1 // fps

#define ILI9341_SPI_HOST VSPI_HOST // VSPI_HOST or HSPI_HOST
