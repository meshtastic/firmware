#define LED_PIN 45 // LED is not populated on earliest board variant
#define BUTTON_PIN 0
#define PIN_BUTTON2 21             // Second built-in button
#define ALT_BUTTON_PIN PIN_BUTTON2 // Send the up event

// I2C
#define I2C_SDA SDA
#define I2C_SCL SCL

// Display (E-Ink)
#define PIN_EINK_CS 5
#define PIN_EINK_BUSY 1
#define PIN_EINK_DC 2
#define PIN_EINK_RES 3
#define PIN_EINK_SCLK 4
#define PIN_EINK_MOSI 6

// SPI
#define SPI_INTERFACES_COUNT 2
#define PIN_SPI_MISO 11
#define PIN_SPI_MOSI 10
#define PIN_SPI_SCK 9

// Power
#define VEXT_ENABLE 18            // Powers the E-Ink display, and the 3.3V supply to the I2C QuickLink connector
#define PERIPHERAL_WARMUP_MS 1000 // Make sure I2C QuickLink has stable power before continuing
#define VEXT_ON_VALUE HIGH
#define ADC_CTRL 46
#define ADC_CTRL_ENABLED HIGH
#define BATTERY_PIN 7
#define ADC_CHANNEL ADC1_GPIO7_CHANNEL
#define ADC_MULTIPLIER 4.9 * 1.03
#define ADC_ATTENUATION ADC_ATTEN_DB_2_5

// LoRa
#define USE_SX1262

#define LORA_DIO0 RADIOLIB_NC // a No connect on the SX1262 module
#define LORA_RESET 12
#define LORA_DIO1 14 // SX1262 IRQ
#define LORA_DIO2 13 // SX1262 BUSY

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