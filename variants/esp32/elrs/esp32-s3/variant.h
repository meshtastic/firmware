// ELRS Target Selection - automatically defined by PlatformIO environment
// #define ELRS_BSIP_SUPERX_MONO_DUAL
// #define ELRS_DIY_TRUE_DIVERSITY_PWM16_S3_2400
// #define ELRS_DIY_TRUE_DIVERSITY_VTX_PWM11_S3_2400
// #define ELRS_SPRACING_RXG1_2400

// Common settings
#undef HAS_GPS
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#undef EXT_NOTIFY_OUT

// Global chip-specific pin mappings
// SX128X mappings (2.4GHz)
#define SX128X_CS LORA_CS
#define SX128X_DIO1 LORA_DIO1
#define SX128X_BUSY LORA_BUSY
#define SX128X_RESET LORA_RESET

// LR1121 mappings (dual-band)
#define LR1121_SPI_NSS_PIN LORA_CS
#define LR1121_SPI_SCK_PIN LORA_SCK
#define LR1121_SPI_MOSI_PIN LORA_MOSI
#define LR1121_SPI_MISO_PIN LORA_MISO
#define LR1121_NRESET_PIN LORA_RESET
#define LR1121_BUSY_PIN LORA_BUSY
#define LR1121_IRQ_PIN LORA_DIO1
#define LR11X0_DIO_AS_RF_SWITCH

// Second radio mappings for true diversity
#define SX128X_CS_2 LORA_CS_2
#define SX128X_DIO0_2 LORA_DIO0_2
#define SX128X_DIO1_2 LORA_DIO1_2
#define SX128X_BUSY_2 LORA_BUSY_2
#define SX128X_RESET_2 LORA_RESET_2
#define LR1121_SPI_NSS_2_PIN LORA_CS_2
#define LR1121_NRESET_2_PIN LORA_RESET_2
#define LR1121_BUSY_2_PIN LORA_BUSY_2
#define LR1121_IRQ_2_PIN LORA_DIO1_2

// bsip superx-mono
#ifdef ELRS_BSIP_SUPERX_MONO_DUAL
#define USE_LR1121
#define TWO_RADIOS

// Radio pins
#define LORA_BUSY 14
#define LORA_CS 4
#define LORA_DIO1 11
#define LORA_MISO 7
#define LORA_MOSI 6
#define LORA_RESET 12
#define LORA_SCK 5

// Second radio pins (true diversity)
#define LORA_BUSY_2 36
#define LORA_CS_2 13
#define LORA_DIO1_2 21
#define LORA_RESET_2 33

// Other pins
#define NEOPIXEL_DATA 2
#define SERIAL_RX_PIN 44
#define SERIAL_TX_PIN 43


// RGB LED
#define HAS_NEOPIXEL
#define NEOPIXEL_COUNT 1
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)
#endif // ELRS_BSIP_SUPERX_MONO_DUAL

// Pin Configuration 2 - Shared by multiple targets
#if defined(ELRS_DIY_TRUE_DIVERSITY_PWM16_S3_2400) || defined(ELRS_DIY_TRUE_DIVERSITY_VTX_PWM11_S3_2400)
#define USE_SX1280
#define TWO_RADIOS

// Radio pins
#define LORA_BUSY 4
#define LORA_CS 17
#define LORA_DIO1 5
#define LORA_MISO 15
#define LORA_MOSI 16
#define LORA_RESET 6
#define LORA_SCK 7

// Second radio pins (true diversity)
#define LORA_BUSY_2 18
#define LORA_CS_2 46
#define LORA_DIO1_2 8
#define LORA_RESET_2 3

// Other pins
#define NEOPIXEL_DATA 48
#define SERIAL_RX_PIN 44
#define SERIAL_TX_PIN 43


// RGB LED
#define HAS_NEOPIXEL
#define NEOPIXEL_COUNT 1
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)
#endif // defined(ELRS_DIY_TRUE_DIVERSITY_PWM16_S3_2400) || defined(ELRS_DIY_TRUE_DIVERSITY_VTX_PWM11_S3_2400)

// spracing rxg1
#ifdef ELRS_SPRACING_RXG1_2400
#define USE_SX1280
#define TWO_RADIOS

// Radio pins
#define LORA_BUSY 7
#define LORA_CS 10
#define LORA_DIO1 6
#define LORA_MISO 13
#define LORA_MOSI 11
#define LORA_RESET 9
#define LORA_SCK 12

// Second radio pins (true diversity)
#define LORA_BUSY_2 5
#define LORA_CS_2 8
#define LORA_DIO1_2 4
#define LORA_RESET_2 46

// Other pins
#define NEOPIXEL_DATA 38
#define SERIAL_RX_PIN 44
#define SERIAL_TX_PIN 43


// RGB LED
#define HAS_NEOPIXEL
#define NEOPIXEL_COUNT 1
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)
#endif // ELRS_SPRACING_RXG1_2400

// Set second radio CS pin high at startup to disable it
#if defined(TWO_RADIOS)
#define PIN_ENABLE_HIGH LORA_CS_2
#endif // TWO_RADIOS

// Ensure only one target is selected
#if defined(ELRS_BSIP_SUPERX_MONO_DUAL) + defined(ELRS_DIY_TRUE_DIVERSITY_PWM16_S3_2400) + defined(ELRS_DIY_TRUE_DIVERSITY_VTX_PWM11_S3_2400) + defined(ELRS_SPRACING_RXG1_2400) != 1
#error "Exactly one ELRS target must be defined"
#endif