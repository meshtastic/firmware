// Status
// #define LED_PIN_POWER 1
// #define BIAS_T_ENABLE LED_PIN_POWER
// #define BIAS_T_VALUE HIGH

#define ELECROW_ThinkNode_M5

#define M5_buzzer 5

#define UART_TX 43
#define UART_RX 44
// LED
#define POWER_LED 3 // red
#define LED_POWER 2
// #define USER_LED 1  //green

// USB_CHECK
#define USB_CHECK 12
#define ADC_V 4

/*
 * Buttons
 */
#define PIN_BUTTON2 14
#define PIN_BUTTON1 21

/*Wire Interfaces*/

#define I2C_SCL 1
#define I2C_SDA 2
// #define I2C_SCL 47
// #define I2C_SDA 48
/*
 * GPS pins
 */
#define GPS_SWITH 10
// #define HAS_GPS 1
#define GPS_L76K
#define PIN_GPS_REINIT 13 // An output to reset L76K GPS. As per datasheet, low for > 100ms will reset the L76K

#define PIN_GPS_STANDBY 11 // An output to wake GPS, low means allow sleep, high means force wake
// Seems to be missing on this new board
// #define PIN_GPS_PPS (32 + 4)  // Pulse per second input from the GPS
#define GPS_TX_PIN 20 // This is for bits going TOWARDS the CPU
#define GPS_RX_PIN 19 // This is for bits going TOWARDS the GPS

#define GPS_THREAD_INTERVAL 50

#define PIN_SERIAL1_RX GPS_TX_PIN
#define PIN_SERIAL1_TX GPS_RX_PIN

// PCF8563 RTC Module
#define PCF8563_RTC 0x51

#define SX126X_CS 17
#define LORA_SCK 16
#define LORA_MOSI 15
#define LORA_MISO 7
#define SX126X_RESET 6
#define SX126X_BUSY 5
#define SX126X_DIO1 4
#define SX126X_DIO2_AS_RF_SWITCH
// #define SX126X_DIO3 9
#define SX126X_DIO3_TCXO_VOLTAGE 3.3
#define SX126X_POWER_EN 46
#define SX126X_MAX_POWER 22 // SX126xInterface.cpp defaults to 22 if not defined, but here we define it for good practice
#define USE_SX1262
#define LORA_CS SX126X_CS // FIXME: for some reason both are used in /src
#define LORA_DIO1 SX126X_DIO1

#define USE_EINK
#define PIN_EINK_EN -1 // Note: this is really just backlight power
#define PCA_PIN_EINK_EN 5
#define PIN_EINK_CS 39
#define PIN_EINK_BUSY 42
#define PIN_EINK_DC 40
#define PIN_EINK_RES 41
#define PIN_EINK_SCLK 38
#define PIN_EINK_MOSI 45 // also called SDI

// Controls power for all peripherals (eink + GPS + LoRa + Sensor)
#define PIN_POWER_EN -1
#define PCA_PIN_POWER_EN 4

// #define PIN_SPI1_MISO  -1
// #define PIN_SPI1_MOSI PIN_EINK_MOSI
// #define PIN_SPI1_SCK PIN_EINK_SCLK
/*
 * SPI Interfaces
 */
// #define SPI_INTERFACES_COUNT 1s

// For LORA, spi 2
#define PIN_SPI_MISO 7
#define PIN_SPI_MOSI 15
#define PIN_SPI_SCK 16

#define BUTTON_PIN PIN_BUTTON1
#define BUTTON_PIN_ALT PIN_BUTTON2
