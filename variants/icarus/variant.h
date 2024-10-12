// For OLED 1.3"
#define USE_SH1106
#define I2C_SDA 8
#define I2C_SCL 9

#define I2C_SDA1 18
#define I2C_SCL1 6

// Buttons
#define BUTTON_PIN 7  // Select
#define BUTTON_NEED_PULLUP
#define BUTTON_PIN2 12 // Down
#define BUTTON_PIN3 13 // Up

// RA-01SH 
#define LORA_RESET 42 // RST for SX1276, and for SX1262/SX1268
#define LORA_DIO1 5  // IRQ for SX1262/SX1268
#define LORA_BUSY 47  // BUSY for SX1262/SX1268


#undef LORA_SCK
#define LORA_SCK 21
#undef LORA_MISO
#define LORA_MISO 39
#undef LORA_MOSI
#define LORA_MOSI 38
#undef LORA_CS
#define LORA_CS 17

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH // use DIO2 as RF switch
#define USE_SX1262

// Battery
#define BATTERY_PIN 1
#define ADC_CHANNEL ADC1_GPIO1_CHANNEL
#define BATTERY_SENSE_SAMPLES 15 // Set the number of samples, It has an effect of increasing sensitivity.
#define ADC_MULTIPLIER 2
