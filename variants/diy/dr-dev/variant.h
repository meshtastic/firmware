// Initialize i2c bus on sd_dat and esp_led pins, respectively. We need a bus to not hang on boot
#define HAS_SCREEN 0
#define I2C_SDA 4
#define I2C_SCL 5
#define BATTERY_PIN 34
#define ADC_CHANNEL ADC1_GPIO34_CHANNEL

// GPS
#undef GPS_RX_PIN
#define GPS_RX_PIN NOT_A_PIN
#define HAS_GPS 0

#define BUTTON_PIN 13 // The middle button GPIO on the T-Beam
#define BUTTON_NEED_PULLUP
#define EXT_NOTIFY_OUT 12 // Overridden default pin to use for Ext Notify Module (#975).

#define LORA_DIO0 NOT_A_PIN  // a No connect on the SX1262/SX1268 module
#define LORA_RESET NOT_A_PIN // RST for SX1276, and for SX1262/SX1268
#define LORA_DIO3 NOT_A_PIN  // Not connected on PCB, but internally on the SX1262/SX1268, if DIO3 is high the TXCO is enabled

// In transmitting, set TXEN as high communication level，RXEN pin is low level;
// In receiving, set RXEN as high communication level, TXEN is lowlevel;
// Before powering off, set TXEN、RXEN as low level.

#undef RF95_SCK
#define RF95_SCK 18
#undef RF95_MISO
#define RF95_MISO 19
#undef RF95_MOSI
#define RF95_MOSI 23

// PINS FOR THE 900M22S

#define LORA_DIO1 26        // IRQ for SX1262/SX1268
#define LORA_DIO2 22        // BUSY for SX1262/SX1268
#define LORA_TXEN NOT_A_PIN // Input - RF switch TX control, connecting external MCU IO or DIO2, valid in high level
#define LORA_RXEN 17        // Input - RF switch RX control, connecting external MCU IO, valid in high level
#undef RF95_NSS
#define RF95_NSS 16
#define SX126X_BUSY 22
#define SX126X_CS 16

// PINS FOR THE 900M30S
/*
#define LORA_DIO1 27  // IRQ for SX1262/SX1268
#define LORA_DIO2 35 // BUSY for SX1262/SX1268
#define LORA_TXEN NOT_A_PIN // Input - RF switch TX control, connecting external MCU IO or DIO2, valid in high level
#define LORA_RXEN 21  // Input - RF switch RX control, connecting external MCU IO, valid in high level
#undef RF95_NSS
#define RF95_NSS 33
#define SX126X_BUSY 35
#define SX126X_CS 33
*/

// RX/TX for RFM95/SX127x
#define RF95_RXEN LORA_RXEN
#define RF95_TXEN LORA_TXEN
// #define RF95_TCXO <GPIO#>

// common pinouts for SX126X modules

#define SX126X_DIO1 LORA_DIO1
#define SX126X_RESET LORA_RESET
#define SX126X_RXEN LORA_RXEN
#define SX126X_TXEN LORA_TXEN

// supported modules list
//#define USE_RF95 // RFM95/SX127x
#define USE_SX1262
//#define USE_SX1268
//#define USE_LLCC68
#define SX126X_E22