void c6l_init();

#define HAS_GPS 1
#define GPS_RX_PIN 4
#define GPS_TX_PIN 5

#define I2C_SDA 10
#define I2C_SCL 8

#define PIN_BUZZER 11

#define HAS_NEOPIXEL                         // Enable the use of neopixels
#define NEOPIXEL_COUNT 1                     // How many neopixels are connected
#define NEOPIXEL_DATA 2                      // gpio pin used to send data to the neopixels
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800) // type of neopixels in use
#define ENABLE_AMBIENTLIGHTING               // Turn on Ambient Lighting

// #define BUTTON_PIN 9
#define BUTTON_EXTENDER

#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

// WaveShare Core1262-868M OK
// https://www.waveshare.com/wiki/Core1262-868M
#define USE_SX1262

#define LORA_MISO 22
#define LORA_SCK 20
#define LORA_MOSI 21
#define LORA_CS 23
#define LORA_RESET RADIOLIB_NC
#define LORA_DIO1 7
#define LORA_BUSY 19
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 3.0

#define USE_SPISSD1306
#ifdef USE_SPISSD1306
#define SSD1306_NSS 6 // CS
#define SSD1306_RS 18 // DC
#define SSD1306_RESET 15
// #define OLED_DG 1
#endif
#define SCREEN_TRANSITION_FRAMERATE 10
#define BRIGHTNESS_DEFAULT 130 // Medium Low Brightness
