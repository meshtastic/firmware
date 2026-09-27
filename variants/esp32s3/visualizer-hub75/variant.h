// Big Visualizer - ESP32-S3-DevKitC-1 (N16R8) driving a 128x64 HUB75 RGB matrix
// via the seengreat "RGB Matrix Adapter Board (E)" Rev. 2.2 (V2.x), plus a
// Wio-SX1262 (for XIAO, Seeed p/n 6379) LoRa header board.

#define BUTTON_PIN 0

#define I2C_SDA 41
#define I2C_SCL 42

// --- Display: HUB75 via seengreat Shield (V2.x, hard-wired) -----------------
#define HAS_SPI_TFT 1             // enable the color BaseUI path (TFTColorRegions/theming)
#define DISPLAY_FORCE_SMALL_FONTS // 128x64 panel: keep OLED-sized fonts, not big TFT fonts
#define USE_HUB75                 // select HUB75Display in Screen.cpp

#define TFT_WIDTH 128
#define TFT_HEIGHT 64
#define HUB75_BRIGHTNESS_DEFAULT 180

// RGB data lines.
// This panel is BGR: R and B are swapped per half vs. the nominal seengreat
// mapping (R1<->B1, R2<->B2), verified at bring-up. Software-only fix; the
// shield board is unchanged. G / address / control lines stay as routed.
#define HUB75_R1 17
#define HUB75_G1 8
#define HUB75_B1 18
#define HUB75_R2 15
#define HUB75_G2 1
#define HUB75_B2 16
// Row-address lines (1/32 scan -> A..E)
#define HUB75_A 7
#define HUB75_B 48
#define HUB75_C 6
#define HUB75_D 47
#define HUB75_E 2
// Control lines
#define HUB75_CLK 5
#define HUB75_LAT 21
#define HUB75_OE 4

// --- Radio: Wio-SX1262 for XIAO (header board, Seeed p/n 6379) ---------------
#define USE_SX1262

#define LORA_SCK 12
#define LORA_MISO 13
#define LORA_MOSI 11
#define LORA_CS 10
#define LORA_RESET 9
#define LORA_DIO1 40

#define SX126X_CS LORA_CS
#define SX126X_SCK LORA_SCK
#define SX126X_MOSI LORA_MOSI
#define SX126X_MISO LORA_MISO
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY 14
#define SX126X_RESET LORA_RESET

#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define SX126X_RXEN 39
#define SX126X_TXEN RADIOLIB_NC

#define SX126X_MAX_POWER 22

#define LED_HEARTBEAT 38
