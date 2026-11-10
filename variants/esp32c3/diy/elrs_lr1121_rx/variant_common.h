// Shared pinout for ExpressLRS "Generic C3 LR1121" single-radio RX modules repurposed as
// Meshtastic nodes: ESP32-C3 + LR1121 (dual-band sub-GHz + 2.4 GHz) + NeoPixel + button, no display.
//
// Pin source: ExpressLRS "Generic C3 LR1121.json" layout file, which is the base layout shared by
// every single-radio C3+LR1121 ExpressLRS RX (BAYCKRC, DAKEFPV, GEPRC, Spedix, THOBBY, HGLRC,
// RadioMaster XR*, Zerodrag, generic, ... -- see ExpressLRS Targets/targets.json). The individual
// products differ only in their RF-switch wiring (radio_rfsw_ctrl overlay) and PA power, not in
// these core pins, so this header is shared and each board picks its own rfswitch_*.h table.

#define HAS_SCREEN 0
#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#undef EXT_NOTIFY_OUT

#define BUTTON_PIN 9

// NeoPixel (GRB, 1 LED)
#define HAS_NEOPIXEL
#define NEOPIXEL_COUNT 1
#define NEOPIXEL_DATA 8
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)

// LR1121 dual-band radio
#define USE_LR1121
#define LORA_SCK 6
#define LORA_MISO 5
#define LORA_MOSI 4
#define LORA_CS 7
#define LR1121_SPI_SCK_PIN LORA_SCK
#define LR1121_SPI_MISO_PIN LORA_MISO
#define LR1121_SPI_MOSI_PIN LORA_MOSI
#define LR1121_SPI_NSS_PIN LORA_CS
#define LR1121_NRESET_PIN 2
#define LR1121_BUSY_PIN 3
#define LR1121_IRQ_PIN 1
// RF switching via LR1121 internal DIOs (DIO5-8). Each board supplies its own table in rfswitch.h,
// decoded from that product's radio_rfsw_ctrl (or the ELRS compiled-in default); not confirmed
// against the board's schematic, re-check if RX is dead on one band.
#define LR11X0_DIO_AS_RF_SWITCH
// No tcxo field in the source ELRS layout -> assume plain crystal, no LR11X0_DIO3_TCXO_VOLTAGE.
// If RX proves flaky, this is the first thing to re-check.

// FC-facing UART (these are RX targets — talk to the flight controller, not a JR bay).
// Not wired into Meshtastic; left here for reference in case a future passthrough feature needs it.
// #define UART_RX_PIN 20
// #define UART_TX_PIN 21
