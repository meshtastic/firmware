#ifndef _VARIANT_KEEPTEEN_LT1_
#define _VARIANT_KEEPTEEN_LT1_

#define VARIANT_MCK             (64000000ul)
#define USE_LFRC 


#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


#define PINS_COUNT  48
#define NUM_DIGITAL_PINS    48
#define NUM_ANALOG_INPUTS   1
#define NUM_ANALOG_OUTPUTS  0


#define LED_PIN 15
#define LED_BUILTIN LED_PIN
#define LED_STATE_ON 1 
#define LED_BLUE -1 // disabled

// OLED display i2c configuration
#define WIRE_INTERFACES_COUNT 1
#define HAS_SCREEN 1
#define USE_SSD1306
#define SDA_OLED 34
#define SCL_OLED 36
#define RST_OLED -1 // nc
#define RESET_OLED RST_OLED
#define PIN_WIRE_SDA SDA_OLED
#define PIN_WIRE_SCL SCL_OLED

// we only have a single button, connected at pin 32
#define BUTTON_PIN  32

// ADC settings for battery monitoring
#define BATTERY_PIN 31
#define ADC_MULTIPLIER 1.73F
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.6F
#define BATTERY_SENSE_RESOLUTION_BITS 12

// serial interface used for GPS
#define PIN_SERIAL1_RX 22
#define PIN_SERIAL1_TX 20

// gps itself
#define GPS_BAUDRATE 9600
#define GPS_RX_PIN PIN_SERIAL1_RX
#define GPS_TX_PIN PIN_SERIAL1_TX
#define PIN_GPS_EN 24

// LoRa module: sx1262
#define SPI_INTERFACES_COUNT 1
#define USE_SX1262
#define LORA_DIO0 -1 // a No connect on the SX1262 module
#define LORA_RESET 9
#define LORA_DIO1 10 // SX1262 IRQ
#define LORA_DIO2 29 // SX1262 BUSY
#define LORA_DIO3   
#define LORA_NSS 45
#define LORA_SCK 43
#define LORA_MISO 2
#define LORA_MOSI 38
#define LORA_CS 8
#define PIN_SPI_MISO LORA_MISO
#define PIN_SPI_MOSI LORA_MOSI
#define PIN_SPI_SCK LORA_SCK

#define SX126X_CS LORA_NSS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_RXEN RADIOLIB_NC
#define SX126X_TXEN RADIOLIB_NC

#define SX126X_DIO2_AS_RF_SWITCH true
#define SX126X_DIO3_TCXO_VOLTAGE 1.8f


// hacks and crap to make it compile
#define PIN_SERIAL2_RX -1
#define PIN_SERIAL2_TX -1

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif // _VARIANT_KEEPTEEN_LT1_