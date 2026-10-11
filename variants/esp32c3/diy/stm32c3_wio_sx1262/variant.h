// variants/esp32c3/supermini_wio_sx1262/variant.h
// ESP32-C3 SuperMini (HW-466AB) + Seeed Wio-SX1262 for XIAO
//
// Распайка (модуль Wio поверх гребёнок SuperMini, как на фото):
//   Wio SCK  -> GPIO2    Wio NSS    -> GPIO9
//   Wio MISO -> GPIO3    Wio BUSY   -> GPIO8
//   Wio MOSI -> GPIO4    Wio RST    -> GPIO7
//   Wio D7   -> GPIO1    Wio DIO1   -> GPIO6
//   Wio D0   -> GPIO5    Wio RF_SW  -> GPIO10
//   Wio D6   -> GPIO20   (GPIO0, GPIO21 свободны)

// Встроенный светодиод (GPIO8) и кнопка BOOT (GPIO9) заняты под BUSY/NSS,
// поэтому LED_PIN / BUTTON_PIN не определяем.
// #define BUTTON_PIN не задан - при необходимости внешняя кнопка на GPIO0 или GPIO5
// #define BUTTON_PIN 0

// ---- LoRa: SX1262 ----
#define USE_SX1262

#define LORA_SCK 2
#define LORA_MISO 3
#define LORA_MOSI 4
#define LORA_CS 9

#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET 7
#define LORA_DIO1 6
#define LORA_DIO2 RADIOLIB_NC
#define LORA_BUSY 8

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET

// RF_SW = RXEN, TX-переключение идёт через DIO2
#define SX126X_RXEN 10
#define SX126X_TXEN RADIOLIB_NC
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// ---- Опционально: GPS на свободных пинах ----
// #define GPS_RX_PIN 20
// #define GPS_TX_PIN 21
