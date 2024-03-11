#define LED_PIN 2     // If defined we will blink this LED
#define BUTTON_PIN 32 // If defined, this will be used for user button presses

#define HAS_ETHERNET 1

#define ETH_TYPE ETH_PHY_RTL8201
#define ETH_ADDR 0
#define ETH_CLK_MODE ETH_CLOCK_GPIO16_OUT
#define ETH_RESET_PIN -1
#define ETH_MDC_PIN 23
#define ETH_POWER_PIN 12
#define ETH_MDIO_PIN 18
#define SD_MISO_PIN 34
#define SD_MOSI_PIN 13
#define SD_SCLK_PIN 14
#define SD_CS_PIN 5

#define USE_SX1262

#define LORA_DIO0 -1 // a No connect on the SX1262 module
#define LORA_RESET 12
#define LORA_DIO1 14 // SX1262 IRQ
#define LORA_DIO2 13 // SX1262 BUSY
#define LORA_DIO3    // Not connected on PCB, but internally on the TTGO SX1262, if DIO3 is high the TXCO is enabled

#define LORA_SCK 9
#define LORA_MISO 11
#define LORA_MOSI 10
#define LORA_CS 8

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET

#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
