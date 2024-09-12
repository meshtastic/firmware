/*
 Initial settings and work by https://github.com/gjelsoe
 Unit provided by Radio Master RC
 https://radiomasterrc.com/products/bandit-expresslrs-rf-module with 1.29" OLED display CH1115 driver
*/

/*
  On this model then screen is NOT upside down, don't flip it for the user.
*/
#undef DISPLAY_FLIP_SCREEN

/*
  I2C SDA and SCL.
  0x18 - STK8XXX Accelerometer
  0x3C - SH1115 Display Driver
*/
#define I2C_SDA 14
#define I2C_SCL 12

/*
  I2C STK8XXX Accelerometer Interrupt PIN to ESP32 Pin 6 - SENSOR_CAPP (GPIO37)
*/
#define STK8XXX_INT 37

/*
  No GPS - but free pins are available.
*/
#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

/*
  Pin connections from ESP32-D0WDQ6 to SX1276.
*/
#define LORA_DIO0 22
#define LORA_DIO1 21
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS 4
#define LORA_RESET 5
#define LORA_TXEN 33

/*
  This unit has a FAN built-in.
  FAN is active at 250mW on it's ExpressLRS Firmware.
  This FAN has TACHO signal on Pin 27 for use with PWM.
*/
#define RF95_FAN_EN 2

/*
  LED PIN setup and it has a NeoPixel LED.
  It's possible to setup colors for Button 1 and 2,
  look at BUTTON1_COLOR, BUTTON1_COLOR_INDEX, BUTTON2_COLOR and BUTTON2_COLOR_INDEX
  this is done here for now.
*/
#define HAS_NEOPIXEL                         // Enable the use of neopixels
#define NEOPIXEL_COUNT 6                     // How many neopixels are connected
#define NEOPIXEL_DATA 15                     // GPIO pin used to send data to the neopixels
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800) // Type of neopixels in use
#define ENABLE_AMBIENTLIGHTING               // Turn on Ambient Lighting
// #define BUTTON1_COLOR 0xFF0000               // Background light for Button 1 in HEX RGB Color (RadioMaster Bandit only).
// #define BUTTON1_COLOR_INDEX 0                // NeoPixel Index ID for Button 1
// #define BUTTON2_COLOR 0x0000FF               // Background light for Button 2 in HEX RGB Color (RadioMaster Bandit only).
// #define BUTTON2_COLOR_INDEX 1                // NeoPixel Index ID for Button 2

/*
  It has 1 x five-way and 2 x normal buttons.

  Button    GPIO    RGB Index
  ---------------------------
  Five-way  39      -
  Button 1  34      0
  Button 2  35      1

  Five way button when using ADC.
  2.632V, 2.177V, 1.598V, 1.055V, 0V

  ADC Values:
  { UP, DOWN, LEFT, RIGHT, ENTER, IDLE }
  3227, 0 ,1961, 2668, 1290, 4095

  Five way button when using ADC.
  https://github.com/ExpressLRS/targets/blob/f3215b5ec891108db1a13523e4163950cfcadaac/TX/Radiomaster%20Bandit.json#L41

*/
#define INPUTBROKER_EXPRESSLRSFIVEWAY_TYPE
#define PIN_JOYSTICK 39
#define JOYSTICK_ADC_VALS /*UP*/ 3227, /*DOWN*/ 0, /*LEFT*/ 1961, /*RIGHT*/ 2668, /*OK*/ 1290, /*IDLE*/ 4095

/*
 Normal Button Pin setup.
*/
#define BUTTON_PIN 34
#define BUTTON_NEED_PULLUP

/*
  No External notification.
*/
#undef EXT_NOTIFY_OUT

/*
  Remapping PIN Names.
  Note, that this unit uses RFO
*/
#define USE_RF95
#define USE_RF95_RFO
#define RF95_CS LORA_CS
#define RF95_DIO1 LORA_DIO1
#define RF95_TXEN LORA_TXEN
#define RF95_RESET LORA_RESET
#define RF95_MAX_POWER 10

/*
  This module has Skyworks SKY66122 controlled by dacWrite
  power ranging from 100mW to 1000mW.

  Mapping of PA_LEVEL to Power output: GPIO26/dacWrite
  168 -> 100mW
  155 -> 250mW
  142 -> 500mW
  110 -> 1000mW
*/
#define RF95_PA_EN 26
#define RF95_PA_DAC_EN
#define RF95_PA_LEVEL 110