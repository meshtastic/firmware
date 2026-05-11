#define HAS_GPS 0
#define HAS_WIRE 0
#define I2C_NO_RESCAN

#define WIFI_LED 5
#define WIFI_STATE_ON 0

#define LED_PIN 6
#define LED_STATE_ON 0
#define BUTTON_PIN 4

#define LORA_SCK 42
#define LORA_MISO 41
#define LORA_MOSI 40
#define LORA_CS 39
#define LORA_RESET 21

#define USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 15
#define SX126X_BUSY 47
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define PIN_POWER_EN 45

#define HAS_ETHERNET 1
#define USE_CH390D 1

#define ETH_MISO_PIN 12
#define ETH_MOSI_PIN 11
#define ETH_SCLK_PIN 13
#define ETH_CS_PIN 14
#define ETH_INT_PIN 10
#define ETH_RST_PIN 9
// #define ETH_ADDR 1
