#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 15 // per @der_bear on the forum, 36 is incorrect for this board type and 15 is a better pick
#define GPS_TX_PIN 13

#define EXT_NOTIFY_OUT 2 // Default pin to use for Ext Notify Module.

#define BATTERY_PIN 35 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
// ratio of voltage divider = 2.0 (R42=100k, R43=100k)
#define ADC_MULTIPLIER 2.11 // 2.0 + 10% for correction of display undervoltage.

#define I2C_SDA 21 // I2C pins for this board
#define I2C_SCL 22

#define LED_PIN 25     // If defined we will blink this LED
#define BUTTON_PIN 12  // If defined, this will be used for user button presses,

#define BUTTON_NEED_PULLUP

#define USE_SX1280
#define LORA_RESET 23

#define SX128X_CS 18 // FIXME - we really should define LORA_CS instead
#define SX128X_DIO1 26
#define SX128X_DIO2 33
#define SX128X_BUSY 32
#define SX128X_RESET LORA_RESET
#define SX128X_E22 // Not really an E22 but TTGO seems to be trying to clone that
// Internally the TTGO module hooks the SX1280-DIO2 in to control the TX/RX switch (which is the default for the sx1280interface
// code)
