#define LED_PIN 18
#define BUTTON_PIN 0

// I2C
#define I2C_SDA SDA
#define I2C_SCL SCL

// Display (E-Ink)
#define USE_EINK
#define PIN_EINK_CS 4
#define PIN_EINK_BUSY 7
#define PIN_EINK_DC 5
#define PIN_EINK_RES 6
#define PIN_EINK_SCLK 3
#define PIN_EINK_MOSI 2

// SPI
#define SPI_INTERFACES_COUNT 2
#define PIN_SPI_MISO 10 // MISO
#define PIN_SPI_MOSI 11 // MOSI
#define PIN_SPI_SCK 9   // SCK

// Power
#define VEXT_ENABLE 45 // Active low, powers the E-Ink display
#define ADC_CTRL 19
#define BATTERY_PIN 20
#define ADC_CHANNEL ADC2_GPIO20_CHANNEL
#define ADC_MULTIPLIER 2                // Voltage divider is roughly 1:1
#define BAT_MEASURE_ADC_UNIT 2          // Use ADC2
#define ADC_ATTENUATION ADC_ATTEN_DB_12 // Voltage divider output is quite high

// LoRa
#define USE_SX1262

#define LORA_DIO0 -1 // a No connect on the SX1262 module
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
