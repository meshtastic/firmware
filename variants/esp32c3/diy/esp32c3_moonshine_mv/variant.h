#define BUTTON_PIN 9

// LED pin on HT-DEV-ESP_V2 and HT-DEV-ESP_V3
// https://resource.heltec.cn/download/HT-CT62/HT-CT62_Reference_Design.pdf
// https://resource.heltec.cn/download/HT-DEV-ESP/HT-DEV-ESP_V3_Sch.pdf


#define LED_POWER 13    // LED
#define LED_STATE_ON 1 // State when LED is lit


#define HAS_SCREEN 1
#define USE_SSD1306

#define HAS_I2C 1
#define WIRE_INTERFACES_COUNT (1)
#define I2C_SDA 0
#define I2C_SCL 1

#define HAS_GPS 1
#define GPS_RX_PIN 21
#define GPS_TX_PIN 20
#define GPS_POWER_TOGGLE 1
#define PIN_GPS_EN 12


#define BATTERY_PIN            2
#define ADC_CHANNEL            ADC1_GPIO2_CHANNEL
#define ADC_MULTIPLIER         2.0f

#define HAS_NEOPIXEL 1
#define NEOPIXEL_DATA 13
#define NEOPIXEL_COUNT 1 // How many neopixels are connected
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)

#define RA_01SC_P

#ifdef RA_01SC_P
#define SETTING_MAX_POWER 29
#define TX_GAIN_LORA 26
#define SX126X_MAX_POWER 3
#endif

#define USE_LLCC68
#define USE_SX1262
#define USE_SX1268

#define LORA_SCK 10
#define LORA_MISO 6
#define LORA_MOSI 7
#define LORA_CS 8
#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET 5
#define LORA_DIO1 3
#define LORA_DIO2 RADIOLIB_NC
#define LORA_BUSY 4
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define TCXO_OPTIONAL