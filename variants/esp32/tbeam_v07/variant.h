// #define BUTTON_NEED_PULLUP // if set we need to turn on the internal CPU pullup during sleep

#define I2C_SDA 21
#define I2C_SCL 22

#define BUTTON_PIN 39
#define BATTERY_PIN 35    // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define EXT_NOTIFY_OUT 13 // Default pin to use for Ext Notify Module.
#define ADC_CHANNEL ADC1_GPIO35_CHANNEL

#define USE_RF95
#define LORA_DIO0 26 // a No connect on the SX1262 module
#define LORA_RESET 23
#define LORA_DIO1 33
#define LORA_DIO2 32 // Not really used

// This board has different GPS pins than all other boards
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 12
#define GPS_TX_PIN 15
#define GPS_UBLOX