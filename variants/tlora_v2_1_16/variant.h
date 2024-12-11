#define BATTERY_PIN 35
#define ADC_CHANNEL ADC1_GPIO35_CHANNEL
#define BATTERY_SENSE_SAMPLES 30

// ratio of voltage divider = 2.0 (R42=100k, R43=100k)
#define ADC_MULTIPLIER 2

#define I2C_SDA 21 // I2C pins for this board
#define I2C_SCL 22

#define LED_PIN 25 // If defined we will blink this LED

#define USE_RF95
#define LORA_DIO0 26 // a No connect on the SX1262 module
#define LORA_RESET 23

// In the T3 V1.6.1 TXCO version, GPIO 33 is connected to Radio’s
// internal temperature-compensated crystal oscillator enable
#ifdef LORA_TCXO_GPIO
#define LORA_DIO1 RADIOLIB_NC // no-connect on sx127x module
#else
#define LORA_DIO1 33 // https://www.thethingsnetwork.org/forum/t/big-esp32-sx127x-topic-part-3/18436
#endif

#define LORA_DIO2 32 // Not really used