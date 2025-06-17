#define I2C_SDA 9
#define I2C_SCL 40

#define USE_SX1262

#define LORA_SCK 5
#define LORA_MISO 3
#define LORA_MOSI 6
#define LORA_CS 7
#define LORA_RESET 8

#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 47
#define SX126X_BUSY 48
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif

#define SX126X_POWER_EN (4)

#define PIN_POWER_EN PIN_3V3_EN
#define PIN_3V3_EN (14)

#define LED_GREEN 46
#define LED_BLUE 45

#define PIN_LED1 LED_GREEN
#define PIN_LED2 LED_BLUE

#define LED_CONN LED_BLUE
#define LED_PIN LED_GREEN
#define ledOff(pin) pinMode(pin, INPUT)

#define LED_STATE_ON 1 // State when LED is litted

#define HAS_GPS 1
#define GPS_TX_PIN 43
#define GPS_RX_PIN 44

#define BATTERY_PIN 1
#define ADC_CHANNEL ADC1_GPIO1_CHANNEL
#define ADC_MULTIPLIER 1.667