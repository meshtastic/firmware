// TODO refactor this out with better radio configuration system
#ifdef USE_RF95

#ifndef RF95_RESET
#define RF95_RESET LORA_RESET
#endif

#ifndef RF95_IRQ
#define RF95_IRQ LORA_DIO0 // on SX1262 version this is a no connect DIO0
#endif

#ifndef RF95_DIO1
#define RF95_DIO1 LORA_DIO1 // Note: not really used for RF95, but used for pure SX127x
#endif

#endif
