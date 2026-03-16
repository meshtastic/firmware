// Raspberry Pi Pico 2 + external W5500 Ethernet module + EBYTE E22-900M30S
// RP2350 (4 MB flash) — wire modules to the GPIO pins listed below
//
// LoRa (SX1262 / E22-900M30S) on SPI1:
//   SCK=GP10  MOSI=GP11  MISO=GP12  CS=GP13
//   RST=GP15  DIO1/IRQ=GP14  BUSY=GP2  RXEN=GP3
//   TXEN: bridge E22_DIO2 → E22_TXEN on the module (no RP2350 GPIO needed)
//
// W5500 Ethernet on SPI0:
//   MISO=GP16  CS=GP17  SCK=GP18  MOSI=GP19  RST=GP20
//
// See wiring.svg in this directory for a complete connection diagram.

// #define RADIOLIB_CUSTOM_ARDUINO 1
// #define RADIOLIB_TONE_UNSUPPORTED 1
// #define RADIOLIB_SOFTWARE_SERIAL_UNSUPPORTED 1

#define ARDUINO_ARCH_AVR

// Onboard LED (GP25 on Pico 2)
#define LED_POWER PIN_LED

// Power monitoring
// GP24: VBUS sense – HIGH when USB is present (digital read)
// GP29: ADC3 measures VSYS/3 (200 kΩ / 100 kΩ divider, same as standard Pico 2)
#define EXT_PWR_DETECT 24
#define BATTERY_PIN    29
#define ADC_MULTIPLIER 3.0
#define BATTERY_SENSE_RESOLUTION_BITS 12
// No real battery — suppress false "battery at 100%" while USB powers VSYS
#define NO_BATTERY_LEVEL_ON_CHARGE

// Optional user button — connect a button between GP6 and GND
// #define BUTTON_PIN 6
// #define BUTTON_NEED_PULLUP

// GPS UART pins (RP2040 UART0 by default: TX=GP0, RX=GP1)
#define GPS_TX_PIN 0
#define GPS_RX_PIN 1

// ---- EBYTE E22-900M30S on SPI1 -----------------------------------------
#define USE_SX1262

#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

#define LORA_SCK  10
#define LORA_MOSI 11
#define LORA_MISO 12
#define LORA_CS   13

#define LORA_DIO0  RADIOLIB_NC
#define LORA_RESET 15
#define LORA_DIO1  14   // IRQ
#define LORA_DIO2  2    // BUSY
#define LORA_DIO3  RADIOLIB_NC

#ifdef USE_SX1262
#define SX126X_CS    LORA_CS
#define SX126X_DIO1  LORA_DIO1
#define SX126X_BUSY  LORA_DIO2
#define SX126X_RESET LORA_RESET
// GP3 = RXEN: driven HIGH at init and held there (LNA always enabled).
// SX1262 drives DIO2 HIGH during TX → TXEN via bridge on E22 module.
#define SX126X_ANT_SW 3
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif

// ---- W5500 Ethernet on SPI0 --------------------------------------------
#define HAS_ETHERNET 1
#define WIZNET_5500_EVB_PICO2 1   // reuses EVB driver code paths

#define ETH_SPI0_MISO 16
#define ETH_SPI0_SCK  18
#define ETH_SPI0_MOSI 19

#define PIN_ETHERNET_RESET 20
#define PIN_ETHERNET_SS    17
#define ETH_SPI_PORT       SPI
