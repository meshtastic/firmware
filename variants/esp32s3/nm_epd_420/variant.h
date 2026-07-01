// RockBase IoT NM-EPD-420 — ESP32-S3 / 4.2" 400×300 tri-color EPD (GDEY042Z98) / SX126x LoRa / AHT20
//
// Pin map authoritative source: https://github.com/RockBase-iot/NM-EPD-420 (src/config.h)
// LoRa wiring and SPI assignment mirror the Heltec Vision Master E290 reference design;
// the EPD also lives on the same FSPI pins as the E290, only the panel size and the aux
// button GPIO differ. AHT20 sits behind a power-gate (GPIO40 HIGH = on) which we expose
// as VEXT_ENABLE so the existing peripheral-power code path drives it at boot. The EPD
// power is hard-wired to 3V3 on this board, so toggling VEXT does not affect the display.

#define LED_PIN 47 // Onboard WS2812 (single pixel)

// Buttons
#define BUTTON_PIN 0               // BOOT — RTC GPIO, wakes from deep sleep
#define PIN_BUTTON2 45             // USER button
#define ALT_BUTTON_PIN PIN_BUTTON2 // Auxiliary input

// I²C
#define I2C_SDA SDA
#define I2C_SCL SCL

// E-Ink display (FSPI / SPI2). 4.2" 400×300 tri-color (B/W/R) — driven as B/W in firmware.
#define PIN_EINK_CS 46
#define PIN_EINK_BUSY 6
#define PIN_EINK_DC 4
#define PIN_EINK_RES 5
#define PIN_EINK_SCLK 2
#define PIN_EINK_MOSI 1

// HSPI bus — LoRa (and µSD, unused by Meshtastic)
#define SPI_INTERFACES_COUNT 2
#define PIN_SPI_MISO 11
#define PIN_SPI_MOSI 10
#define PIN_SPI_SCK 9

// AHT20 temperature/humidity sensor power gate. Driving HIGH energises the sensor
// rail; the EPD does not share this rail, so it stays available continuously.
#define VEXT_ENABLE 40
#define VEXT_ON_VALUE HIGH
#define PERIPHERAL_WARMUP_MS 50 // AHT20 needs ~20 ms after power-up before first I²C transaction

// External audio amp shutdown — keep the Class-D PA disabled so we don't draw ~3 mA idle.
// We define this as an output that is forced LOW in the variant init path of GPIO that
// the ESP32-S3 boot ROM has already left floating; the codec itself is suspended through
// I²C by other code paths if/when audio support is added.
#define PIN_AMP_ENABLE 41

// Battery monitoring: GPIO43 enables the divider, GPIO3 reads battery voltage.
#define ADC_CTRL 43
#define ADC_CTRL_ENABLED HIGH
#define BATTERY_PIN 3 // ADC1_CH2
#define ADC_CHANNEL ADC_CHANNEL_2
#define ADC_MULTIPLIER 2.0
#define ADC_ATTENUATION ADC_ATTEN_DB_12

// LoRa — SX1262 on HSPI, matches Heltec Vision Master E290 wiring exactly
#define USE_SX1262

#define LORA_DIO0 RADIOLIB_NC // SX1262 has no DIO0
#define LORA_RESET 12
#define LORA_DIO1 14 // SX1262 IRQ
#define LORA_DIO2 13 // SX1262 BUSY

#define LORA_SCK 9
#define LORA_MISO 11
#define LORA_MOSI 10
#define LORA_CS 8

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET

#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
