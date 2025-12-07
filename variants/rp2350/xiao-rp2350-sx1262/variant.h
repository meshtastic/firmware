// XIAO RP2350 with Wio-SX1262 Hat Configuration
// Board: seeed_xiao_rp2350 (official Seeed board definition)
// D-pin definitions are provided by the board's pins_arduino.h

#define ARDUINO_ARCH_AVR

// Button and LED (use board defaults)
#define BUTTON_PIN D0         // GPIO26 - Boot button
#define LED_PIN PIN_LED       // GPIO25 - Built-in LED

// Battery monitoring
#define BATTERY_PIN 26        // GPIO26/A0 - Battery voltage sense
#define ADC_MULTIPLIER 2.0    // Voltage divider ratio
#define BATTERY_SENSE_RESOLUTION_BITS 12  // RP2350 ADC is 12-bit

// ============================================================================
// LoRa SX1262 Configuration (Wio-SX1262 Hat)
// ============================================================================
// Using D-pin notation (defined by seeed_xiao_rp2350 board)
// Matches XIAO NRF52840 Wio-SX1262 hat pinout

#define USE_SX1262

// SPI pin definitions (required by Meshtastic)
#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

#define LORA_SCK D8          // GPIO2 - SPI0 SCK
#define LORA_MISO D9         // GPIO4 - SPI0 MISO
#define LORA_MOSI D10        // GPIO3 - SPI0 MOSI
#define LORA_CS D4           // GPIO6 - Chip Select

// SX1262 Control Pins
#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET D2        // GPIO28
#define LORA_DIO1 D1         // GPIO27
#define LORA_DIO2 D3         // GPIO5 (Busy)
#define LORA_DIO3 RADIOLIB_NC

// SX126X specific configuration (matching XIAO NRF52840 exactly)
#ifdef USE_SX1262
#define SX126X_CS D4          // GPIO6 - Chip Select
#define SX126X_DIO1 D1        // GPIO27 - Interrupt
#define SX126X_BUSY D3        // GPIO5 - Busy
#define SX126X_RESET D2       // GPIO28 - Reset
#define SX126X_RXEN D5        // GPIO7 - RF Switch
#define SX126X_TXEN RADIOLIB_NC
#define SX126X_DIO2_AS_RF_SWITCH  // DIO2 controls TX/RX switch
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif

// Wio-SX1262 Hat Pinout Reference:
// D1  (GPIO27) → SX1262 DIO1 (Interrupt)
// D2  (GPIO28) → SX1262 RESET
// D3  (GPIO5)  → SX1262 BUSY
// D4  (GPIO6)  → SX1262 CS (Chip Select)
// D5  (GPIO7)  → SX1262 RXEN (RF Switch)
// D8  (GPIO2)  → SPI SCK
// D9  (GPIO4)  → SPI MISO
// D10 (GPIO3)  → SPI MOSI
