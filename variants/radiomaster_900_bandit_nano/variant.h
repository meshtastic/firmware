/*
 Initial settings and work by https://github.com/uberhalit and re-work by https://github.com/gjelsoe
 Unit provided by Radio Master RC
 https://radiomasterrc.com/products/bandit-nano-expresslrs-rf-module with 0.96" OLED display
*/

/*
  I2C SDA and SCL.
*/
#define I2C_SDA 14
#define I2C_SCL 12

/*
  No GPS - but free solder pads are available inside the case.
*/
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
*/
#define RF95_FAN_EN 2

/*
  LED PIN setup.
*/
#define LED_PIN 15

/*
  Five way button when using ADC.
  2.632V, 2.177V, 1.598V, 1.055V, 0V

  Possible ADC Values:
  { UP, DOWN, LEFT, RIGHT, ENTER, IDLE }
  3227, 0 ,1961, 2668, 1290, 4095
*/
#define BUTTON_PIN 39
#define BUTTON_NEED_PULLUP

#define SCREEN_ROTATE

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
#define RF95_MAX_POWER 12

/*
  This module has Skyworks SKY66122 controlled by dacWrite
  power rangeing from 100mW to 1000mW.

  Mapping of PA_LEVEL to Power output: GPIO26/dacWrite
  168 -> 100mW  -> 2.11v
  148 -> 250mW  -> 1.87v
  128 -> 500mW  -> 1.63v
  90  -> 1000mW -> 1.16v
*/
#define RF95_PA_EN 26
#define RF95_PA_DAC_EN
#define RF95_PA_LEVEL 90