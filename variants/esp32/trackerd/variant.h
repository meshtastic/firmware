// Initialize i2c bus on sd_dat and esp_led pins, respectively. We need a bus to not hang on boot
#define HAS_SCREEN 0
#define I2C_SDA 21
#define I2C_SCL 22

#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 9
#define GPS_TX_PIN 10

#define LED_PIN 13 // 13 red, 2 blue, 15 red

// #define HAS_BUTTON 0
#define BUTTON_PIN 0
#define BUTTON_NEED_PULLUP

#define USE_RF95
#define LORA_DIO0 26 // a No connect on the SX1262 module
#define LORA_RESET 23
#define LORA_DIO1 33
#define LORA_DIO2 32 // Not really used

#undef BAT_MEASURE_ADC_UNIT
#define BATTERY_PIN 35      // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define ADC_MULTIPLIER 1.34 //  tracked resistance divider is 100k+470k, so it can not fillfull well on esp32 adc
#define ADC_CHANNEL ADC1_GPIO35_CHANNEL
#define ADC_ATTENUATION ADC_ATTEN_DB_12 // lower dB for high resistance voltage divider

#undef GPS_RX_PIN
#undef GPS_TX_PIN
#undef PIN_GPS_PPS

#define PIN_GPS_EN 12
#define GPS_EN_ACTIVE 1

#define GPS_TX_PIN 10
#define GPS_RX_PIN 9

#define PIN_GPS_RESET 25
// #define PIN_GPS_REINIT 25
#define GPS_RESET_MODE 1

#define GPS_L76K

#undef PIN_LED1
#undef PIN_LED2
#undef PIN_LED3

#define PIN_LED1 13
#define PIN_LED2 15
#define PIN_LED3 2

#define ledOff(pin) pinMode(pin, INPUT)