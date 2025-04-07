// #define RADIOLIB_CUSTOM_ARDUINO 1
// #define RADIOLIB_TONE_UNSUPPORTED 1
// #define RADIOLIB_SOFTWARE_SERIAL_UNSUPPORTED 1

#define ARDUINO_ARCH_AVR

// Define I2C pins to ensure correct usage of both ports
#define I2C_SDA 20
#define I2C_SCL 21
#define I2C_SDA1 2
#define I2C_SCL1 3

#define LED_CONN PIN_LED2
#define LED_PIN LED_BUILTIN
#define ledOff(pin) pinMode(pin, INPUT)

#define BUTTON_PIN 9
#define BUTTON_NEED_PULLUP
// #define EXT_NOTIFY_OUT 4

#define BATTERY_PIN 26
#define BATTERY_SENSE_RESOLUTION_BITS ADC_RESOLUTION
// ratio of voltage divider = 3.0 (R17=200k, R18=100k)
#define ADC_MULTIPLIER 1.84

#define DETECTION_SENSOR_EN 28

#define USE_SX1262

#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

// RAK BSP somehow uses SPI1 instead of SPI0
#define HW_SPI1_DEVICE
#define LORA_SCK (10u)
#define LORA_MOSI (11u)
#define LORA_MISO (12u)
#define LORA_CS (13u)

#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET 14
#define LORA_DIO1 29
#define LORA_DIO2 15
#define LORA_DIO3 RADIOLIB_NC

#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_POWER_EN 25
// DIO2 controlls an antenna switch and the TCXO voltage is controlled by DIO3
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif

#define HAS_ETHERNET 1
#define PIN_ETHERNET_RESET 7 // IO3
#define PIN_ETHERNET_SS 17
#define ETH_SPI_PORT SPI

#define PIN_ETH_POWER_EN 22
