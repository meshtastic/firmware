#define I2C_SDA 12
#define I2C_SCL 11

#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

#define LORA_SCK 36
#define LORA_MISO 35
#define LORA_MOSI 37
#define LORA_CS 6 // NSS

#define USE_RF95
#define LORA_DIO0 14          // IRQ
#define LORA_RESET 5          // RESET
#define LORA_RST 5            // RESET
#define LORA_IRQ 14           // DIO0
#define LORA_DIO1 RADIOLIB_NC // Not really used
#define LORA_DIO2 RADIOLIB_NC // Not really used

#define HAS_AXP2101
