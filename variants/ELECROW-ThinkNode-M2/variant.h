// Status
#define LED_PIN 1

#define PIN_BUTTON1 47 // 功能键
#define PIN_BUTTON2 4  // 电源键

#define LED_POWER 6
#define ADC_V 42
// USB_CHECK
#define EXT_PWR_DETECT 7

#define PIN_BUZZER 5

#define I2C_SCL 15
#define I2C_SDA 16

#define UART_TX 43
#define UART_RX 44

#define VEXT_ENABLE 46 // for OLED
#define VEXT_ON_VALUE HIGH

#define SX126X_CS 10
#define LORA_SCK 12
#define LORA_MOSI 11
#define LORA_MISO 13
#define SX126X_RESET 21
#define SX126X_BUSY 14
#define SX126X_DIO1 3
#define SX126X_DIO2_AS_RF_SWITCH
// #define SX126X_DIO3 9
#define SX126X_DIO3_TCXO_VOLTAGE 3.3

#define SX126X_MAX_POWER 22 // SX126xInterface.cpp defaults to 22 if not defined, but here we define it for good practice
#define USE_SX1262
#define LORA_CS SX126X_CS // FIXME: for some reason both are used in /src
#define LORA_DIO1 SX126X_DIO1
#define SX126X_POWER_EN 48

// Battery
// #define BATTERY_PIN 2
#define BATTERY_PIN 17
// #define ADC_CHANNEL ADC1_GPIO2_CHANNEL
#define ADC_CHANNEL ADC2_GPIO17_CHANNEL
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER (1.548F)
#define BAT_MEASURE_ADC_UNIT 2

#define HAS_SCREEN 1
#define USE_SH1106 1

// PCF8563 RTC Module
// #define PCF8563_RTC 0x51
// #define PIN_RTC_INT 48 // Interrupt from the PCF8563 RTC
#define HAS_RTC 0
#define HAS_GPS 0

#define BUTTON_PIN PIN_BUTTON1
#define BUTTON_PIN_ALT PIN_BUTTON2
