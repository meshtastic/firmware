// LilyGo T-Beam-1W variant.h
// Configuration based on LilyGO utilities.h and RF documentation

// I2C for OLED display (SH1106 at 0x3C)
#define I2C_SDA 8
#define I2C_SCL 9

// GPS - Quectel L76K
#define GPS_RX_PIN 5
#define GPS_TX_PIN 6
#define GPS_1PPS_PIN 7
#define GPS_WAKEUP_PIN 16 // GPS_EN_PIN in LilyGO code
#define HAS_GPS 1
#define GPS_BAUDRATE 9600

// Buttons
#define BUTTON_PIN 0      // BUTTON 1
#define ALT_BUTTON_PIN 17 // BUTTON 2

// SPI (shared by LoRa and SD)
#define SPI_MOSI 11
#define SPI_SCK 13
#define SPI_MISO 12
#define SPI_CS 10

// SD Card
#define HAS_SDCARD
#define SDCARD_USE_SPI1
#define SDCARD_CS SPI_CS

// LoRa Radio - SX1262 with 1W PA
#define USE_SX1262

#define LORA_SCK SPI_SCK
#define LORA_MISO SPI_MISO
#define LORA_MOSI SPI_MOSI
#define LORA_CS 15
#define LORA_RESET 3
#define LORA_DIO1 1
#define LORA_BUSY 38

// CRITICAL: Radio power enable - MUST be HIGH before lora.begin()!
// GPIO 40 powers the SX1262 + PA module via LDO
#define SX126X_POWER_EN 40

// TX power offset for external PA (0 = no offset, full SX1262 power)
#define TX_GAIN_LORA 10

#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET

// RF switching configuration for 1W PA module
// DIO2 controls PA (via SX126X_DIO2_AS_RF_SWITCH)
// CTRL PIN (GPIO 21) controls LNA - must be HIGH during RX
// Truth table: DIO2=1,CTRL=0 → TX (PA on, LNA off)
//              DIO2=0,CTRL=1 → RX (PA off, LNA on)
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_RXEN 21 // LNA enable - HIGH during RX

// TCXO voltage - required for radio init
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define SX126X_MAX_POWER 22
#endif

// LED
#define LED_PIN 18
#define LED_STATE_ON 1 // HIGH = ON

// Battery ADC
#define BATTERY_PIN 4
#define ADC_CHANNEL ADC1_GPIO4_CHANNEL
#define BATTERY_SENSE_SAMPLES 30
#define ADC_MULTIPLIER 2.9333

// NTC temperature sensor
#define NTC_PIN 14

// Fan control
#define FAN_CTRL_PIN 41
// Meshtastic standard fan control pin macro
#define RF95_FAN_EN FAN_CTRL_PIN

// PA Ramp Time - T-Beam 1W requires >800us stabilization (default is 200us)
// Value 0x05 = RADIOLIB_SX126X_PA_RAMP_800U
#define SX126X_PA_RAMP_US 0x05

// Display - SH1106 OLED (128x64)
#define USE_SH1106
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

// 32768 Hz crystal present
#define HAS_32768HZ 1
