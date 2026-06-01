#define ARDUINO_ARCH_AVR

// #define BUTTON_PIN -1
#define LED_POWER PIN_LED

// no ADC by default
#define BATTERY_PIN -1
// ratio of voltage divider = 3.0 (R1=200k, R2=100k)
#define ADC_MULTIPLIER 3
// ADC_RESOLUTION is missing upstream, hardcode it here.
#define BATTERY_SENSE_RESOLUTION_BITS 12

#define USE_SX1262

#define LORA_SCK 2
#define LORA_MISO 4
#define LORA_MOSI 3

#define SX126X_CS 6
#define SX126X_DIO1 27
#define SX126X_BUSY 5
#define SX126X_RESET 28
#define SX126X_RXEN 7

#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8