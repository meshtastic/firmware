#define TFT_BL 2
#define SPI_FREQUENCY 2000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 142
#define TFT_WIDTH 428
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define TFT_OFFSET_ROTATION 0
#define SCREEN_TRANSITION_FRAMERATE 5
#define HAS_SCREEN 1
#define TFT_BLACK 0
#define BRIGHTNESS_DEFAULT 130 // Medium Low Brightness
#define USE_TFTDISPLAY 1

#define USE_POWERSAVE
#define SLEEP_TIME 120

#define GPS_DEFAULT_NOT_PRESENT 1

// keyboard
#define I2C_SDA 47 // I2C pins for this board
#define I2C_SCL 14
// #define KB_BL_PIN 46                   // not used for now
#define KB_INT 13

#define TFT_DC 39
#define TFT_CS 41

// LoRa
#define USE_SX1262

#define LORA_SCK 8
#define LORA_MISO 9
#define LORA_MOSI 3
#define LORA_CS 17

#define LORA_RESET 18
#define LORA_DIO1 16 // SX1262 IRQ
#define LORA_DIO2 15 // SX1262 BUSY

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET

#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define LED_NOTIFICATION 1
#define LED_STATE_ON 0
