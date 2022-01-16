#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 5
#define GPS_TX_PIN 18
#define GPS_RESET_N 10
#define GPS_EXTINT 23 // On MAX-M8 module pin name is EXTINT. On L70 module pin name is STANDBY.

#define BATTERY_PIN 39    // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define BATTERY_EN_PIN 14 // Voltage voltage divider enable pin connected to mosfet

#define I2C_SDA 4 // I2C pins for this board
#define I2C_SCL 2

#define LED_PIN 12 // If defined we will blink this LED
//#define BUTTON_PIN 36  // If defined, this will be used for user button presses (ToDo problem on that line on debug screen -->
// Long press start!) #define BUTTON_NEED_PULLUP //GPIOs 34 to 39 are GPIs – input only pins. These pins don’t have internal
// pull-ups or pull-down resistors.

#define USE_RF95
#define LORA_DIO0 38 // a No connect on the SX1262 module
#define LORA_RESET 9

#define RF95_SCK 22
#define RF95_MISO 19
#define RF95_MOSI 13
#define RF95_NSS 21