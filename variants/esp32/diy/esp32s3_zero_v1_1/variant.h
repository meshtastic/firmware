// For OLED LCD
// #define I2C_SDA 21
// #define I2C_SCL 22

// GPS
// #undef GPS_RX_PIN
// #define GPS_RX_PIN 15
#define HAS_SCREEN 0
#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#define BUTTON_PIN 2
#define BUTTON_NEED_PULLUP
#define EXT_NOTIFY_OUT 1 // Overridden default pin to use for Ext Notify Module (#975).

#define LORA_DIO0 RADIOLIB_NC  // a No connect on the SX1262/SX1268 module
#define LORA_RESET 5 // RST for SX1276, and for SX1262/SX1268
#define LORA_DIO1 3  // IRQ for SX1262/SX1268
#define LORA_DIO2 4  // BUSY for SX1262/SX1268
#define LORA_DIO3     // Not connected on PCB, but internally on the TTGO SX1262/SX1268, if DIO3 is high the TXCO is enabled

// In transmitting, set TXEN as high communication level，RXEN pin is low level;
// In receiving, set RXEN as high communication level, TXEN is lowlevel;
// Before powering off, set TXEN、RXEN as low level.
#define LORA_RXEN 11 // Input - RF switch RX control, connecting external MCU IO, valid in high level
#define LORA_TXEN 12   // Input - RF switch TX control, connecting external MCU IO or DIO2, valid in high level

#define LORA_SCK 10
#define LORA_MISO 6
#define LORA_MOSI 7
#define LORA_CS 8

// RX/TX for RFM95/SX127x
#define RF95_RXEN LORA_RXEN
#define RF95_TXEN LORA_TXEN
// #define RF95_TCXO <GPIO#>

// common pinouts for SX126X modules
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_RXEN LORA_RXEN
#define SX126X_TXEN LORA_TXEN

// supported modules list
#define USE_RF95 // RFM95/SX127x
#define USE_SX1262
// #define USE_SX1268
// #define USE_LLCC68

// #define SX126X_POWER_EN (21)
// DIO2 controlls an antenna switch and the TCXO voltage is controlled by DIO3
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#ifdef EBYTE_E22
// Internally the TTGO module hooks the SX126x-DIO2 in to control the TX/RX switch
// (which is the default for the sx1262interface code)
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define TCXO_OPTIONAL // make it so that the firmware can try both TCXO and XTAL
#endif
