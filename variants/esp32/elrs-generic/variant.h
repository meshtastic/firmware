#ifndef ELRS_TARGETS_H
#define ELRS_TARGETS_H

// Target selection - uncomment ONE of these
// #define ELRS_GENERIC_900
#define ELRS_GENERIC_2400
// #define ELRS_GENERIC_LR1121 // Currently only the LR1121 True Diversity target is supported


// #define HAS_DUAL_RADIO TODO for the funture, currently does nothing.


#undef HAS_GPS
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#undef EXT_NOTIFY_OUT

//==============================================================================
// ELRS GENERIC 900 MHz TARGET
//==============================================================================
#ifdef ELRS_GENERIC_900

// Chip selection
#define USE_RF95

// Serial pins
#define SERIAL_RX_PIN 3
#define SERIAL_TX_PIN 1

// Radio pins
#define LORA_DIO0 4
#define LORA_DIO1 5
#define LORA_MISO 12
#define LORA_MOSI 13
#define LORA_CS 15
#define LORA_SCK 14
#define LORA_RESET 2

// GPIO
#define LED_PIN 16
#define BUTTON_PIN 0
#define BUTTON_NEED_PULLUP

#endif // ELRS_GENERIC_900

//==============================================================================
// ELRS GENERIC 2.4 GHz TARGET
//==============================================================================
#ifdef ELRS_GENERIC_2400

// Chip selection
#define USE_SX1280

// Serial pins
#define SERIAL_RX_PIN 3
#define SERIAL_TX_PIN 1

// Radio pins
#define LORA_BUSY 5
#define LORA_DIO1 4
#define LORA_MISO 12
#define LORA_MOSI 13
#define LORA_CS 15
#define LORA_SCK 14
#define LORA_RESET 2

#define SX128X_CS LORA_CS
#define SX128X_DIO1 LORA_DIO1
#define SX128X_BUSY LORA_BUSY
#define SX128X_RESET LORA_RESET
#define SX128X_MAX_POWER 13

// GPIO
#define LED_PIN 16
#define BUTTON_PIN 0
#define BUTTON_NEED_PULLUP

#endif // ELRS_GENERIC_2400

//==============================================================================
// ELRS TRUE DIVERSITY LR1121 TARGET
//==============================================================================
#ifdef ELRS_GENERIC_LR1121

// Chip selection
// #define USE_SX1262
#define USE_LR1121


// Board has RGB LED 21
#define HAS_NEOPIXEL                         // Enable the use of neopixels
#define NEOPIXEL_COUNT 1                     // How many neopixels are connected
#define NEOPIXEL_DATA 22                     // gpio pin used to send data to the neopixels
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800) // type of neopixels in use

// GPIO
#define BUTTON_PIN 0 // This is the BOOT button
#define BUTTON_NEED_PULLUP

// Radio pins
#define LORA_MISO 33
#define LORA_MOSI 32
#define LORA_SCK 25
#define LORA_CS 27
#define LORA_RESET 26
#define LORA_DIO1 37

#define LR1121_IRQ_PIN 37
#define LR1121_NRESET_PIN 26
#define LR1121_BUSY_PIN 36
#define LR1121_SPI_NSS_PIN 27
#define LR1121_SPI_SCK_PIN 25
#define LR1121_SPI_MOSI_PIN 32
#define LR1121_SPI_MISO_PIN 33
#define LR11X0_DIO_AS_RF_SWITCH

#endif // ELRS_GENERIC_LR1121


// Ensure only one target is selected
#if defined(ELRS_GENERIC_900) + defined(ELRS_GENERIC_2400) + defined(ELRS_GENERIC_LR1121) != 1
#error "Exactly one ELRS target must be defined"
#endif

#endif // ELRS_TARGETS_H