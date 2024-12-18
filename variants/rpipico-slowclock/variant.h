#define ARDUINO_ARCH_AVR

// Build with slow system clock enabled to reduce power consumption.
#define RP2040_SLOW_CLOCK

#ifdef RP2040_SLOW_CLOCK
// Redefine UART1 serial log output to avoid collision with UART0 for GPS.
#define SERIAL2_TX 4
#define SERIAL2_RX 5
// Reroute log output in SensorLib when USB is not available
#define log_e(...) Serial2.printf(__VA_ARGS__)
#define log_i(...) Serial2.printf(__VA_ARGS__)
#define log_d(...) Serial2.printf(__VA_ARGS__)
#endif

// Expecting the Waveshare Pico GPS hat
#define HAS_GPS 1

// Enable OLED Screen
#define HAS_SCREEN 1
#define USE_SH1106 1
#define RESET_OLED 13

// Redefine I2C0 pins to avoid collision with UART1/Serial2.
#define I2C_SDA 8
#define I2C_SCL 9

// Redefine Waveshare UPS-A/B I2C_1 pins:
#define I2C_SDA1 6
#define I2C_SCL1 7
// Waveshare UPS-A/B uses a 0.01 Ohm shunt for the INA219 sensor
#define INA219_MULTIPLIER 10.0f

// Waveshare Pico GPS L76B pins:
#define GPS_RX_PIN 1
#define GPS_TX_PIN 0

// Wakeup from backup mode
// #define PIN_GPS_FORCE_ON 14
// No GPS reset available
#undef PIN_GPS_RESET
/*
 * For PPS output the resistor R20 needs to be populated with 0 Ohm
 * on the Waveshare Pico GPS board.
 */
#define PIN_GPS_PPS 16
/*
 * For standby mode switching the resistor R18 needs to be populated
 * with 0 Ohm on the Waveshare Pico GPS board.
 */
#define PIN_GPS_STANDBY 17

#define BUTTON_PIN 18
#define EXT_NOTIFY_OUT 22
#define LED_PIN PIN_LED

#define BATTERY_PIN 26
// ratio of voltage divider = 3.0 (R17=200k, R18=100k)
#define ADC_MULTIPLIER 3.1 // 3.0 + a bit for being optimistic
#define BATTERY_SENSE_RESOLUTION_BITS ADC_RESOLUTION

#define USE_SX1262

#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

#define LORA_SCK 10
#define LORA_MISO 12
#define LORA_MOSI 11
#define LORA_CS 3

#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET 15
#define LORA_DIO1 20
#define LORA_DIO2 2
#define LORA_DIO3 RADIOLIB_NC

#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif