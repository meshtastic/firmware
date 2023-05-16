// the default ESP32 Pin of 15 is the Oled SCL, set to 36 and 37 and works fine.
// Tested on Neo6m module.
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 36
#define GPS_TX_PIN 33

#ifndef USE_JTAG  // gpio15 is TDO for JTAG, so no I2C on this board while doing jtag
#define I2C_SDA 4 // I2C pins for this board
#define I2C_SCL 15
#endif

#define RESET_OLED 16 // If defined, this pin will be used to reset the display controller

#define LED_PIN 25   // If defined we will blink this LED
#define BUTTON_PIN 0 // If defined, this will be used for user button presses

#define USE_RF95
#define LORA_DIO0 26 // a No connect on the SX1262 module
#ifndef USE_JTAG
#define LORA_RESET 14
#endif
#define LORA_DIO1 RADIOLIB_NC
#define LORA_DIO2 32 // Not really used

// ratio of voltage divider = 3.20 (R1=100k, R2=220k)
#define ADC_MULTIPLIER 3.2

#define BATTERY_PIN 13 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define ADC_CHANNEL ADC2_GPIO13_CHANNEL
#define BAT_MEASURE_ADC_UNIT 2