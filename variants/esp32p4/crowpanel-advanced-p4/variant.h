// I2C pins for touch screen or ext connector
#define I2C_SDA 7
#define I2C_SCL 8

#define I2C_SDA1 45
#define I2C_SCL1 46

#define USE_POWERSAVE
#define WAKE_ON_TOUCH
#define SCREEN_TOUCH_INT 42
#define SLEEP_TIME 180

// LoRa
#define USE_SX1262
#define LORA_SCK 26
#define LORA_MISO 47
#define LORA_MOSI 48
#define LORA_CS 30
#define LORA_RESET 32

#define SX126X_CS LORA_CS
#define SX126X_DIO1 31
#define SX126X_BUSY 29
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 3.3
