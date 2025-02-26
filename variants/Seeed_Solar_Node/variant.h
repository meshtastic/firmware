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

 #ifndef _SEEED_XIAO_NRF52840_SENSE_H_
 #define _SEEED_XIAO_NRF52840_SENSE_H_
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
 #define LED_RED     15      // P0.11 Red LED
 #define LED_BLUE    19      // P0.12 Blue LED
 #define LED_GREEN   15     // P0.13 Green LED
 
 #define PIN_LED1    LED_GREEN  // Default user LED
 #define PIN_LED2    LED_BLUE   // Secondary indicator
 #define PIN_LED3    LED_RED    // Status LED
 #define LED_BUILTIN PIN_LED1
 #define LED_STATE_ON 1         // Active-high LEDs
 
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 // Button Configuration
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 #define PIN_BUTTON1         5       // P0.05 User button
 #define BUTTON_NEED_PULLUP          // Requires  pull-up
 
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
 #define HAS_WIRE 1
 #define PIN_WIRE_SDA  (9)   // P1.11 (D6)
 #define PIN_WIRE_SCL  (10)   // P1.12 (D7)
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
 
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 // Power Management
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 #define VBAT_ENABLE    14     // P0.14 Battery sense enable
 #define BAT_READ       PIN_VBAT
 #define ADC_MULTIPLIER 3.0f   // Voltage divider ratio (1M+510K)
 #define CHARGE_LED     23     // P0.17 Charging indicator
 #define HICHG          22     // P0.13 Charge current select (High=100mA)
 
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 // Peripheral Configuration
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 // QSPI Flash (MX25R6435F)
 #define PIN_QSPI_SCK   24  // P0.24
 #define PIN_QSPI_CS    25  // P0.25
 #define PIN_QSPI_IO0   26  // P0.26
 #define PIN_QSPI_IO1   27  // P0.27
 #define PIN_QSPI_IO2   28  // P0.28
 #define PIN_QSPI_IO3   29  // P0.29
 
//  // NFC Interface
//  #define PIN_NFC1      30     // P0.09 NFC antenna 1
//  #define PIN_NFC2      31     // P0.10 NFC antenna 2
 
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 // Sensor Configuration (Sense Variant)
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 #ifdef XIAO_SENSE
   #define PIN_LSM6DS3TR_C_POWER 15  // IMU power control
   #define PIN_LSM6DS3TR_C_INT1  18  // IMU interrupt
   #define PIN_PDM_PWR           19  // Microphone power
   #define PIN_PDM_CLK           20  // PDM clock
   #define PIN_PDM_DIN           21  // PDM data
 #endif
 
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 // Compatibility Definitions
 //━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 #ifdef __cplusplus
 extern "C" {
 #endif
 // Serial port placeholders
 #define PIN_SERIAL1_RX (-1)
 #define PIN_SERIAL1_TX (-1)
 #define PIN_SERIAL2_RX (-1)
 #define PIN_SERIAL2_TX (-1)
 #ifdef __cplusplus
 }
 #endif
 
 #endif // _SEEED_XIAO_NRF52840_SENSE_H_