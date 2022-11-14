// Pine64 uses a common pinout for their SX1262 vs RF95 modules - both can be enabled and we will probe at runtime for RF95 and if
// not found then probe for SX1262.  Currently the RF95 code is disabled because I think the RF95 module won't need to ship.
// #define USE_RF95
#define USE_SX1262

// Fake SPI device selections
#define RF95_SCK 5
#define RF95_MISO 19
#define RF95_MOSI 27
#define RF95_NSS RADIOLIB_NC // the ch341f spi controller does CS for us

#define LORA_DIO0 26 // a No connect on the SX1262 module
#define LORA_RESET 14
#define LORA_DIO1 33 // SX1262 IRQ, called DIO0 on pinelora schematic, pin 7 on ch341f "ack" - FIXME, enable hwints in linux
#define LORA_DIO2 32 // SX1262 BUSY, actually connected to "DIO5" on pinelora schematic, pin 8 on ch341f "slct" 
#define LORA_DIO3    // Not connected on PCB, but internally on the TTGO SX1262, if DIO3 is high the TXCO is enabled

#ifdef USE_SX1262
#define SX126X_CS 20 // CS0 on pinelora schematic, hooked to gpio D0 on ch341f
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
// HOPE RFM90 does not have a TCXO therefore not SX126X_E22 
#endif

