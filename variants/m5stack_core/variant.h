// #define BUTTON_NEED_PULLUP // if set we need to turn on the internal CPU pullup during sleep

#define I2C_SDA 21
#define I2C_SCL 22

// #define BUTTON_PIN 39 // 38, 37
//#define BUTTON_PIN 0
#define BUTTON_NEED_PULLUP
// #define EXT_NOTIFY_OUT 13 // Default pin to use for Ext Notify Plugin.

#define BUTTON_PIN 38

#define PIN_BUZZER 25
#define TONE_PIN_CHANNEL 0

#undef RF95_SCK
#undef RF95_MISO
#undef RF95_MOSI
#undef RF95_NSS

#define RF95_SCK 18
#define RF95_MISO 19
#define RF95_MOSI 23
#define RF95_NSS 5

#define USE_RF95
#define LORA_DIO0 36 // a No connect on the SX1262 module
#define LORA_RESET 26
#define LORA_DIO1 RADIOLIB_NC// Not really used
#define LORA_DIO2 RADIOLIB_NC// Not really used

// This board has different GPS pins than all other boards
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17

// Define if screen should be mirrored left to right
#define SCREEN_ROTATE

// LCD screens are slow, so slowdown the wipe so it looks better
#define SCREEN_TRANSITION_FRAMERATE 1 // fps
