#define HAS_WIRE 0
#define I2C_SDA1 45
#define I2C_SCL1 46

#define USE_POWERSAVE
#define WAKE_ON_TOUCH
#define SCREEN_TOUCH_INT 42
#define SLEEP_TIME 180

#if defined(CROWPANEL_ADV_P4_50)

// use UART3-IN for GPS (UART1 can not work with lora)
#define GPS_DEFAULT_NOT_PRESENT 1
#define GPS_RX_PIN 28
#define GPS_TX_PIN 27

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

#elif defined(CROWPANEL_ADV_P4_70_90_101)

// use UART1 for GPS
#define GPS_DEFAULT_NOT_PRESENT 1
#define GPS_RX_PIN 48
#define GPS_TX_PIN 47

// LoRa
#define USE_SX1262
#define LORA_SCK 8
#define LORA_MISO 7
#define LORA_MOSI 6
#define LORA_CS 10
#define LORA_RESET 54

#define SX126X_CS LORA_CS
#define SX126X_DIO1 53
#define SX126X_BUSY 9
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 3.3

#endif
