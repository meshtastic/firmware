#define LED_PIN 33
#define LED_PIN2 34
#define EXT_PWR_DETECT 35

#define BUTTON_PIN 18

#define BATTERY_PIN 7 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define ADC_CHANNEL ADC1_GPIO7_CHANNEL
#define ADC_ATTENUATION ADC_ATTEN_DB_2_5 // lower dB for high resistance voltage divider
#define ADC_MULTIPLIER (4.9 * 1.045)
#define ADC_CTRL 36 // active HIGH, powers the voltage divider. Only on 1.1
#define ADC_CTRL_ENABLED HIGH

#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 5
#define GPS_TX_PIN 4
#define PIN_GPS_RESET 3
#define GPS_RESET_MODE LOW
#define PIN_GPS_PPS 1
#define PIN_GPS_EN 21
#define GPS_EN_ACTIVE HIGH

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
