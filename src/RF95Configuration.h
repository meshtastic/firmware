// TODO refactor this out with better radio configuration system
#ifdef USE_RF95
#define RF95_RESET LORA_RESET
#define RF95_IRQ LORA_DIO0  // on SX1262 version this is a no connect DIO0
#define RF95_DIO1 LORA_DIO1 // Note: not really used for RF95, but used for pure SX127x
#define RF95_DIO2 LORA_DIO2 // Note: not really used for RF95
#endif