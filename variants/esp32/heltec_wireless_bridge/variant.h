
// updated variant 20250420 berlincount, tested with HTIT-TB
//
// connections in HTIT-WB
// per https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf
// md5: a0e6ae10ff76611aa61433366b2e4f5c  esp32_datasheet_en.pdf
// per https://resource.heltec.cn/download/Wireless_Bridge/Schematic_Diagram_HTIT-WB_V0.2.pdf
// md5: d5c1b0219ece347dd8cee866d7d3ab0a  Schematic_Diagram_HTIT-WB_V0.2.pdf

#define NO_EXT_GPIO 1
#define NO_GPS 1

#define HAS_GPS 0 // GPS is not equipped
#undef GPS_RX_PIN
#undef GPS_TX_PIN

// Green / Lora = PIN 22 / GPIO2, Yellow / Wifi = PIN 23 / GPIO0, Blue / BLE = PIN 25 / GPIO16
#define LED_PIN 22
#define WIFI_LED 23
#define BLE_LED 25

// ESP32-D0WDQ6 direct pins SX1276
#define USE_RF95
#define LORA_DIO0 26
#define LORA_DIO1 35
#define LORA_DIO2 34
#define LORA_SCK 05
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS 18

// several things are not possible with JTAG enabled
#ifndef USE_JTAG
#define LORA_RESET 14 // LoRa Reset shares a pin with MTMS
#define I2C_SDA 4     // SD_DATA1 going to W25Q64, but
#define I2C_SCL 15    // SD_CMD shared a pin with MTD0
#endif

// user button is present on device, but currently untested & unconfigured - couldn't figure out how it's connected

// battery support is present within device, but currently untested & unconfigured - couldn't find reliable information yet
