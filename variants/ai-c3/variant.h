#define SDA 0
#define SCL 1
#define I2C_SDA SDA
#define I2C_SCL SCL

#define BUTTON_PIN 9 // BOOT button
#define LED_PIN 30   // RGB LED

#define USE_RF95
#define LORA_SCK 4
#define LORA_MISO 5
#define LORA_MOSI 6
#define LORA_CS 7

#define LORA_DIO0 10
#define LORA_DIO1 3
#define LORA_RESET 2

// WaveShare Core1262-868M
// https://www.waveshare.com/wiki/Core1262-868M
#define USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY 10
#define SX126X_RESET LORA_RESET

#define SX126X_DIO2_AS_RF_SWITCH // use DIO2 as RF switch
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN
