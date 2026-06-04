#ifndef HAS_TFT
#define BUTTON_PIN 0
#define PIN_BUTTON2 21             // Second built-in button
#define ALT_BUTTON_PIN PIN_BUTTON2 // Send the up event

// I2C
#define I2C_SDA SDA
#define I2C_SCL SCL

// Display (TFT)
#define USE_ST7789
#define ST7789_NSS 39
#define ST7789_RS 47  // DC
#define ST7789_SDA 48 // MOSI
#define ST7789_SCK 38
#define ST7789_RESET 40
#define ST7789_MISO 4
#define ST7789_BUSY -1
#define VTFT_CTRL 7
#define VTFT_LEDA 17
#define TFT_BACKLIGHT_ON HIGH
#define ST7789_SPI_HOST SPI2_HOST
#define SPI_FREQUENCY 10000000
#define SPI_READ_FREQUENCY 10000000
#define TFT_HEIGHT 170
#define TFT_WIDTH 320
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
// #define TFT_OFFSET_ROTATION 0
// #define SCREEN_ROTATE
// #define SCREEN_TRANSITION_FRAMERATE 5
#define BRIGHTNESS_DEFAULT 100 // Medium Low Brightnes

// #define SLEEP_TIME 120

// SPI
#define SPI_INTERFACES_COUNT 2
#define PIN_SPI_MISO 11
#define PIN_SPI_MOSI 10
#define PIN_SPI_SCK 9

// Power
#define VEXT_ENABLE 5
#define VEXT_ON_VALUE HIGH
#define ADC_CTRL 46
#define ADC_CTRL_ENABLED HIGH
#define BATTERY_PIN 6
#define ADC_CHANNEL ADC1_GPIO6_CHANNEL
#define ADC_MULTIPLIER 4.9 * 1.03        // Voltage divider is roughly 1:1
#define ADC_ATTENUATION ADC_ATTEN_DB_2_5 // Voltage divider output is quite high

// LoRa
#define USE_SX1262

#define LORA_DIO0 RADIOLIB_NC // a No connect on the SX1262 module
#define LORA_RESET 12
#define LORA_DIO1 14 // SX1262 IRQ
#define LORA_DIO2 13 // SX1262 BUSY
#define LORA_DIO3    // Not connected on PCB, but internally on the TTGO SX1262, if DIO3 is high the TXCO is enabled

#define LORA_SCK 9
#define LORA_MISO 11
#define LORA_MOSI 10
#define LORA_CS 8

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET

#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif // HAS_TFT