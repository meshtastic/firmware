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
// #define GPS_RX_PIN 44
// #define GPS_TX_PIN 43

// #define BATTERY_PIN 4 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
//  ratio of voltage divider = 2.0 (RD2=100k, RD3=100k)
// #define ADC_MULTIPLIER 2.11 // 2.0 + 10% for correction of display undervoltage.
// #define ADC_CHANNEL ADC1_GPIO4_CHANNEL

// keyboard
#define I2C_SDA 47 // I2C pins for this board
#define I2C_SCL 14
// #define KB_POWERON -1                  // must be set to HIGH
// #define KB_SLAVE_ADDRESS TDECK_KB_ADDR // 0x55
// #define KB_BL_PIN 46                   // not used for now
#define KB_INT 13
#define CANNED_MESSAGE_MODULE_ENABLE 1

#define TFT_DC 39
#define TFT_CS 41

// LoRa
#define USE_SX1262

#define LORA_SCK 8
#define LORA_MISO 9
#define LORA_MOSI 3
#define LORA_CS 17

// #define LORA_DIO0 -1 // a No connect on the SX1262 module
#define LORA_RESET 18
#define LORA_DIO1 16 // SX1262 IRQ
#define LORA_DIO2 15 // SX1262 BUSY
// #define LORA_DIO3    // Not connected on PCB, but internally on the TTGO SX1262, if DIO3 is high the TXCO is enabled

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET

#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// #define LED_PIN 1