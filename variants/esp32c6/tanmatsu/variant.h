#define HAS_WIRE 0
#undef SDA
#undef SCL
#undef I2C_SDA
#undef I2C_SCL

#define LORA_SCK 0
#define LORA_MISO 3
#define LORA_MOSI 2
#define LORA_CS 1

#define LORA_DIO0 RADIOLIB_NC
#define LORA_DIO1 4
#define LORA_DIO4 5
#define LORA_RESET 10

#define USE_SX1262
#define USE_SX1268
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY 5
#define SX126X_RESET LORA_RESET

#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define TCXO_OPTIONAL     // make it so that the firmware can try both TCXO and XTAL
extern float tcxoVoltage; // make this available everywhere

#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#define HAS_SCREEN 0
