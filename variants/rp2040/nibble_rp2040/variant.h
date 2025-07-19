#define ARDUINO_ARCH_AVR

#define BUTTON_PIN -1 // Pin 17 used for antenna switching via DIO4

#define LED_PIN 1

#define HAS_CPU_SHUTDOWN 1

#define USE_RF95
#define LORA_SCK 10
#define LORA_MISO 12
#define LORA_MOSI 11
#define LORA_CS 13

#define LORA_DIO0 14
#define LORA_RESET 15
#define LORA_DIO1 RADIOLIB_NC
#define LORA_DIO2 RADIOLIB_NC
