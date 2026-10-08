// ChiselAndCap MESH-DONGLE V0.1
// ESP32-S3-WROOM-1-N16R8 (16MB flash, 8MB octal PSRAM) + SX1262, USB on native GPIO19/20

// ---- Buttons / LEDs ----
#define BUTTON_PIN 0 // SW1, also the BOOT button (10k pull-up to 3V3, press = GND)

#define LED_POWER 4 // LED_BLUE (LED_AMBER is on GPIO5)
#define LED_LORA 5
#define LED_STATE_ON 1 // ASSUMES active-high. Change to 0 if the LED lights when the pin is LOW

// ---- SX1262 LoRa radio ----
#define USE_SX1262

#define LORA_SCK 9
#define LORA_MOSI 10
#define LORA_MISO 11
#define LORA_CS 8
#define LORA_RESET 12
#define LORA_DIO1 14 // SX1262 IRQ
#define LORA_DIO2 13 // SX1262 BUSY (same convention as the Heltec V3 variant)

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET

#define SX126X_DIO2_AS_RF_SWITCH // DIO2 drives the TPE4259-63 RF switch
#define SX126X_DIO3_TCXO_VOLTAGE 3.3 // TODO: set to the supply voltage of the OW2EL89CENUXFMYLC-32M TCXO (datasheet)