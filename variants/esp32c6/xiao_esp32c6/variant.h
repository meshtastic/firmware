/*
  XIAO ESP32C6 + Wio SX1262 for XIAO
  Basato sul variant M5Stack Unit C6L (SPI hardware, stesso chip)
  Differenze: niente TCXO (XTAL), pin diversi, niente OLED/GPS
*/

#define I2C_SDA 22
#define I2C_SCL 23

#define LED_POWER 15
#define LED_STATE_ON 1

// LoRa SX1262 (Wio SX1262 for XIAO)
#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

#define USE_SX1262
#define LORA_SCK  19
#define LORA_MISO 20
#define LORA_MOSI 18
#define LORA_CS   23
#define SX126X_CS     LORA_CS
#define SX126X_DIO1   1
#define SX126X_DIO2   0
#define SX126X_BUSY   21
#define SX126X_RESET  2
#define LORA_RESET     SX126X_RESET

// Wio SX1262: DIO2 controlla RF switch TX/RX
// Il comando SetDio2AsRfSwitch (0x17) non è supportato dal modulo Wio
// -> patchato in RadioLib, usiamo TXEN manuale su GPIO0
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_TXEN 0   // GPIO0 = DIO2: HIGH=TX, LOW=RX

// NO TCXO - Wio SX1262 usa XTAL
// NON definire SX126X_DIO3_TCXO_VOLTAGE

#define SERIAL_PRINT_PORT 1
