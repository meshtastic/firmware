#define LED_PIN 18

#define _VARIANT_HELTEC_WIRELESS_TRACKER
#define HELTEC_TRACKER_V1_X

// I2C
#define I2C_SDA SDA
#define I2C_SCL SCL

// ST7735S TFT LCD
#define ST7735S 1 // there are different (sub-)versions of ST7735
#define ST7735_CS 38
#define ST7735_RS 40  // DC
#define ST7735_SDA 42 // MOSI
#define ST7735_SCK 41
#define ST7735_RESET 39
#define ST7735_MISO -1
#define ST7735_BUSY -1
#define TFT_BL 21 /* V1.1 PCB marking */
#define ST7735_SPI_HOST SPI3_HOST
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define SCREEN_ROTATE
#define TFT_HEIGHT DISPLAY_WIDTH
#define TFT_WIDTH DISPLAY_HEIGHT
#define TFT_OFFSET_X 26
#define TFT_OFFSET_Y -1
#define SCREEN_TRANSITION_FRAMERATE 3 // fps
#define DISPLAY_FORCE_SMALL_FONTS

// pin 3 is Vext on v1.1 - HIGH enables LDO for Vext rail which goes to:
// GPS UC6580:          GPS V_DET(8), VDD_IO(7), DCDC_IN(21), pulls up RESETN(17), D_SEL(33) and BOOT_MODE(34) through 10kR
// GPS LNA SW7125DE:    VCC(4), pulls up SHDN(5) through 10kR
// LED:                 VDD, LEDA (through diode)

#define VEXT_ENABLE 3 // active HIGH - powers the GPS, GPS LNA and OLED
#define VEXT_ON_VALUE HIGH
#define BUTTON_PIN 0

#define BATTERY_PIN 1 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define ADC_CHANNEL ADC1_GPIO1_CHANNEL
#define ADC_ATTENUATION ADC_ATTEN_DB_2_5 // lower dB for high resistance voltage divider
#define ADC_MULTIPLIER 4.9 * 1.045
#define ADC_CTRL 2     // active HIGH, powers the voltage divider. Only on 1.1
#define ADC_USE_PULLUP // Use internal pullup/pulldown instead of actively driving the output

#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 33
#define GPS_TX_PIN 34
#define PIN_GPS_RESET 35
#define PIN_GPS_PPS 36
// #define PIN_GPS_EN 3    // Uncomment to power off the GPS with triple-click on Tracker v1.1, though we'll also lose the
// display.

#define GPS_RESET_MODE LOW
#define GPS_UC6580
#define GPS_BAUDRATE 115200

#define USE_SX1262
#define LORA_DIO0 -1 // a No connect on the SX1262 module
#define LORA_RESET 12
#define LORA_DIO1 14 // SX1262 IRQ
#define LORA_DIO2 13 // SX1262 BUSY
#define LORA_DIO3    // Not connected on PCB, but internally on the TTGO SX1262, if DIO3 is high the TXCO is enabled

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