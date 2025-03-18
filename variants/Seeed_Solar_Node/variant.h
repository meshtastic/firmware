/* 
 * Seeed_XIAO_nRF52840_Sense.h - Pin definitions for Seeed Studio XIAO nRF52840 & Wio SX1262
 * 
 * Compatible with:
 * - Seeed XIAO BLE nRF52840 Sense (https://www.seeedstudio.com/Seeed-XIAO-BLE-nRF52840-p-5201.html)
 * - Seeed Wio SX1262 LoRaWAN Gateway (https://www.seeedstudio.com/Wio-SX1262-with-XIAO-ESP32S3-p-5982.html)
 * 
 * Created [Date]
 * By [Author]
 * Version 1.0
 * License: MIT
 */

 #ifndef _SEEED_SOLAR_NODE_H_
 #define  _SEEED_SOLAR_NODE_H_
 #include "WVariant.h"
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 // Clock Configuration
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 #define VARIANT_MCK       (64000000ul)  // Master clock frequency
 #define USE_LFXO                        // 32.768kHz crystal for LFCLK
 
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 // Pin Capacity Definitions
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 #define PINS_COUNT          (33u)       // Total GPIO pins
 #define NUM_DIGITAL_PINS    (33u)       // Digital I/O pins
 #define NUM_ANALOG_INPUTS   (8u)        // Analog inputs (A0-A5 + VBAT + AREF)
 #define NUM_ANALOG_OUTPUTS  (0u)
 
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 // LED Configuration
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

// LEDs
// LEDs
#define PIN_LED1 (11) // LED        P1.15
#define PIN_LED2 (12)      //

#define LED_BUILTIN PIN_LED1
#define LED_CONN PIN_LED2

#define LED_GREEN PIN_LED1
#define LED_BLUE PIN_LED2
#define LED_PIN PIN_LED2
#define LED_STATE_ON 1 // State when LED is litted
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 // Button Configuration
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 #define BUTTON_PIN D13 // This is the Program Button
 // #define BUTTON_NEED_PULLUP   1
 #define BUTTON_ACTIVE_LOW true
 #define BUTTON_ACTIVE_PULLUP false
 
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 // Digital Pin Mapping (D0-D10)
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 #define D0  0   // P0.02 GNSS_WAKEUP/IO0
 #define D1  1   // P0.03 LORA_DIO1
 #define D2  2   // P0.28 LORA_RESET
 #define D3  3   // P0.29 LORA_BUSY
 #define D4  4   // P0.04 LORA_CS/I2C_SDA
 #define D5  5   // P0.05 LORA_SW/I2C_SCL
 #define D6  6   // P1.11 GNSS_TX
 #define D7  7   // P1.12 GNSS_RX
 #define D8  8   // P1.13 SPI_SCK
 #define D9  9   // P1.14 SPI_MISO
 #define D10 10  // P1.15 SPI_MOSI
 #define D13 13  // P1.01 User Button
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 // Analog Pin Definitions
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 #define PIN_A0    0   // P0.02 Analog Input 0
 #define PIN_A1    1   // P0.03 Analog Input 1
 #define PIN_A2    2   // P0.28 Analog Input 2  
 #define PIN_A3    3   // P0.29 Analog Input 3
 #define PIN_A4    4   // P0.04 Analog Input 4
 #define PIN_A5    5   // P0.05 Analog Input 5
 #define PIN_VBAT  32  // P0.31 Battery voltage sense
 #define ADC_RESOLUTION 12  // 12-bit ADC
 
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 // Communication Interfaces
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 // I2C Configuration
 //#define HAS_WIRE 1
 #define PIN_WIRE_SDA  14   // P0.09
 #define PIN_WIRE_SCL  15   // P0.10
 #define WIRE_INTERFACES_COUNT 1
 #define I2C_NO_RESCAN  

static const uint8_t SDA = PIN_WIRE_SDA;
static const uint8_t SCL = PIN_WIRE_SCL;
 // SPI Configuration (SX1262)
 
 #define SPI_INTERFACES_COUNT 1
 #define PIN_SPI_MISO 9    // P1.14 (D9)
 #define PIN_SPI_MOSI 10   // P1.15 (D10)
 #define PIN_SPI_SCK  8    // P1.13 (D8)
 
 // SX1262 LoRa Module Pins
 #define USE_SX1262
 #define SX126X_CS     D4     // Chip select
 #define SX126X_DIO1   D1     // Digital IO 1 (Interrupt)
 #define SX126X_BUSY    D3     // Busy status
 #define SX126X_RESET   D2     // Reset control
 #define SX126X_DIO3_TCXO_VOLTAGE 1.8  // TCXO supply voltage
 #define SX126X_RXEN    D5     // RX enable control
 #define SX126X_TXEN RADIOLIB_NC
 #define SX126X_DIO2_AS_RF_SWITCH     // This Line is really necessary for SX1262  to work with RF switch or will loss TX power
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 // Power Management
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 #define VBAT_ENABLE    14     // P0.14 Battery sense enable
 #define BAT_READ       PIN_VBAT
 #define ADC_MULTIPLIER 3.0f   // Voltage divider ratio (1M+510K)
 #define CHARGE_LED     23     // P0.17 Charging indicator
 #define HICHG          22     // P0.13 Charge current select (High=100mA)
 
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 // GPS L76KB 
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#define GPS_L76K
#ifdef GPS_L76K
#define PIN_GPS_RX D6 // 44
#define PIN_GPS_TX D7// 43
#define HAS_GPS 1
#define GPS_BAUDRATE 9600
#define GPS_THREAD_INTERVAL 50
#define PIN_SERIAL1_RX PIN_GPS_TX
#define PIN_SERIAL1_TX PIN_GPS_RX
// #define GPS_SLEEP_INT D0
//#define GPS_DEBUG
#define PIN_GPS_STANDBY D0
#endif

// On-board QSPI Flash
#define PIN_QSPI_SCK (18)
#define PIN_QSPI_CS  (19)
#define PIN_QSPI_IO0 (20)
#define PIN_QSPI_IO1 (21)
#define PIN_QSPI_IO2 (22)
#define PIN_QSPI_IO3 (23)

#define EXTERNAL_FLASH_DEVICES P25Q16H
#define EXTERNAL_FLASH_USE_QSPI

 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 // Compatibility Definitions
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 #ifdef __cplusplus
 extern "C" {
 #endif
 // Serial port placeholders

 #define PIN_SERIAL2_RX (-1)
 #define PIN_SERIAL2_TX (-1)
 #ifdef __cplusplus
 }
 #endif
 
 #endif //  _SEEED_SOLAR_NODE_H_