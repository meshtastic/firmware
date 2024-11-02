// Icarus has a 1.3 inch OLED Screen
#define SCREEN_SSD106

#define I2C_SDA 8
#define I2C_SCL 9

#define I2C_SDA1 18
#define I2C_SCL1 6

#define BUTTON_PIN 7 // Selection button

// RA-01SH/HT-RA62 LORA module
#define USE_SX1262

#define LORA_MISO 39
#define LORA_SCK 21
#define LORA_MOSI 38
#define LORA_CS 17

#define LORA_RESET 42
#define LORA_DIO1 5

#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY 47
#define SX126X_RESET LORA_RESET

//  DIO2 controlls an antenna switch
#define SX126X_DIO2_AS_RF_SWITCH
#endif
