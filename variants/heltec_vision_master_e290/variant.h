// #define LED_PIN 18

// Enable bus for external periherals
#define I2C_SDA SDA
#define I2C_SCL SCL

#define USE_EINK

/*
 * eink display pins
 */
#define PIN_EINK_CS 3
#define PIN_EINK_BUSY 5
#define PIN_EINK_DC 4
#define PIN_EINK_RES 5
#define PIN_EINK_SCLK 2
#define PIN_EINK_MOSI 1

/*
 * SPI interfaces
 */
#define SPI_INTERFACES_COUNT 2

#define PIN_SPI_MISO 10 // MISO
#define PIN_SPI_MOSI 11 // MOSI
#define PIN_SPI_SCK 9   // SCK

#define VEXT_ENABLE 18 // powers the e-ink display
#define VEXT_ON_VALUE 1
#define BUTTON_PIN 0

#define ADC_CTRL 46
#define ADC_CTRL_ENABLED HIGH
#define BATTERY_PIN 7
#define ADC_CHANNEL ADC1_GPIO7_CHANNEL
#define ADC_MULTIPLIER 4.9 * 1.03
#define ADC_ATTENUATION ADC_ATTEN_DB_2_5 // Voltage divider output is quite high

#define USE_SX1262

#define LORA_DIO0 -1 // a No connect on the SX1262 module
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