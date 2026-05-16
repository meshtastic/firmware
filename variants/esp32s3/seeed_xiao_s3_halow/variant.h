/*
 * Seeed Studio XIAO ESP32-S3 + Wi-Fi HaLow add-on (Quectel FGH100M-H / Morse Micro MM6108)
 *
 * Pin map fixed by the Seeed HaLow add-on hardware. SCK/MOSI/MISO are the
 * same XIAO pins the LoRa hat uses (7/9/8), but CS/IRQ/RST/WAKE/BUSY collide
 * with the L76K GPS standby (GPIO 1) and I2C SDA (GPIO 5) used by the LoRa
 * variant — so this variant deliberately ships without LoRa, GPS, or the
 * SSD1306 OLED. The HaLow module replaces all of them.
 *
 * Hardware: https://wiki.seeedstudio.com/getting_started_with_wifi_halow_module_for_xiao/
 */

#define LED_POWER 48
#define LED_STATE_ON 1

#define BUTTON_PIN 21
#define BUTTON_NEED_PULLUP

// No battery monitoring on this variant — leaving BATTERY_PIN undefined skips
// the legacy adc1_* ADC code in Power.cpp that isn't compatible with IDF 5.1.

// HaLow BUSY claims GPIO 5 (the I2C SDA used by the LoRa variant), and HaLow
// RST claims GPIO 1 (the GPS standby). Disable I2C, the OLED screen, and GPS
// so nothing else drives those lines.
#define HAS_WIRE 0
#define HAS_SCREEN 0
#define HAS_GPS 0
#define NO_GPS 1

// HaLow module pin map (Seeed XIAO HaLow add-on, fixed by hardware)
#define HALOW_SPI_SCK 7
#define HALOW_SPI_MISO 8
#define HALOW_SPI_MOSI 9
#define HALOW_CS 4
#define HALOW_IRQ 3
#define HALOW_RST 1
#define HALOW_WAKE 2
#define HALOW_BUSY 5
