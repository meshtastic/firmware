#define LED_POWER 45 // LED is not populated on earliest board variant
#define BUTTON_PIN 0
#define PIN_BUTTON2 21             // Second built-in button
#define ALT_BUTTON_PIN PIN_BUTTON2 // Send the up event

/*
 * GPS pins
 */

#define GPS_L76K
#define HAS_GPS 1
#define PIN_GPS_RESET 40// An output to reset L76K GPS. As per datasheet, low for > 100ms will reset the L76K
#define GPS_RESET_MODE LOW
#define PIN_GPS_EN 47
#define GPS_EN_ACTIVE LOW
#define PERIPHERAL_WARMUP_MS 1000 // Make sure I2C QuickLink has stable power before continuing
#define PIN_GPS_STANDBY 42      // An output to wake GPS, low means allow sleep, high means force wake
#define PIN_GPS_PPS 41
// Seems to be missing on this new board
#define GPS_TX_PIN 44 // This is for bits going TOWARDS the CPU
#define GPS_RX_PIN 43 // This is for bits going TOWARDS the GPS
#define GPS_THREAD_INTERVAL 50

// I2C
#define I2C_SDA SDA
#define I2C_SCL SCL

// Display (E-Ink)
#define PIN_EINK_CS 3
#define PIN_EINK_BUSY 6
#define PIN_EINK_DC 4
#define PIN_EINK_RES 5
#define PIN_EINK_SCLK 2
#define PIN_EINK_MOSI 1

// SPI
#define SPI_INTERFACES_COUNT 2
#define PIN_SPI_MISO 11
#define PIN_SPI_MOSI 10
#define PIN_SPI_SCK 9

// Power
#define VEXT_ENABLE 18 // Powers the E-Ink display only
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