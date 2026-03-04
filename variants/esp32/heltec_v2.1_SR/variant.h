// Pin planning should refer to this document
// https://resource.heltec.cn/download/WiFi_LoRa_32/WIFI_LoRa_32_V2.pdf

// the default ESP32 Pin of 15 is the Oled SCL, 37 is battery pin.
// Tested on Neo6m module.
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 36
#define GPS_TX_PIN 33

// GPIO 38 is input-only on ESP32 (GPIO 34-39); do not use as output. No hardware GPS enable on this board.
#undef PIN_GPS_EN

#ifndef USE_JTAG  // gpio15 is TDO for JTAG, so no I2C on this board while doing jtag
#define I2C_SDA 4 // I2C pins for this board
#define I2C_SCL 15
#endif

#define RESET_OLED 16 // If defined, this pin will be used to reset the display controller

#define VEXT_ENABLE 21 // active low, powers the oled display and the lora antenna boost
#define PERIPHERAL_WARMUP_MS 500 // Time for VEXT and LoRa module to stabilize after power-on
#define LED_PIN 25     // If defined we will blink this LED
#define LED_POWER 25   // Status LED (used by main.cpp and StatusLEDModule)
#define BUTTON_PIN 0   // If defined, this will be used for user button presses

#define USE_SX1276
#define USE_RF95
#define LORA_DIO0 26   // SX1276 DIO0
#ifndef USE_JTAG
#define LORA_RESET 14  // SX1276 RST (required for radio init; gpio15 is TDO for JTAG)
#else
#define LORA_RESET RADIOLIB_NC
#endif
#define LORA_DIO1 35 // https://www.thethingsnetwork.org/forum/t/big-esp32-sx127x-topic-part-3/18436
#define LORA_DIO2 34 // Not really used

#define LORA_CS 18       // Compatibility with variant file configuration structure
#define LORA_SCK 5     // Compatibility with variant file configuration structure
#define LORA_MOSI 27   // Compatibility with variant file configuration structure
#define LORA_MISO 19   // Compatibility with variant file configuration structure

#define ADC_MULTIPLIER 3.2 // 220k + 100k (320k/100k=3.2)
// #define ADC_WIDTH ADC_WIDTH_BIT_10

#define BATTERY_PIN 37 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define ADC_CHANNEL ADC1_GPIO37_CHANNEL
#define EXT_NOTIFY_OUT 13 // Default pin to use for Ext Notify Module.