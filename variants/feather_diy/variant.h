// For OLED LCD
#define I2C_SDA 22
#define I2C_SCL 23

#define BUTTON_PIN 7

#define LORA_DIO0 -1  // a No connect on the SX1262/SX1268 module
#define LORA_RESET 13 // RST for SX1276, and for SX1262/SX1268
#define LORA_DIO1 11  // IRQ for SX1262/SX1268
#define LORA_DIO2 12  // BUSY for SX1262/SX1268
#define LORA_DIO3     // Not connected on PCB, but internally on the TTGO SX1262/SX1268, if DIO3 is high the TXCO is enabled

#define RF95_SCK SCK
#define RF95_MISO MI
#define RF95_MOSI MO
#define RF95_NSS D2

// supported modules list
#define USE_SX1262

// common pinouts for SX126X modules
#define SX126X_CS RF95_NSS // NSS for SX126X
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_RXEN 10
#define SX126X_TXEN 9

#ifdef EBYTE_E22
// Internally the TTGO module hooks the SX126x-DIO2 in to control the TX/RX switch
// (which is the default for the sx1262interface code)
#define SX126X_E22
#endif
