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

#define BATTERY_PIN 35 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage

// Battery
// The battery sense is hooked to pin A0 (4)
// it is defined in the anlaolgue pin section of this file
// and has 12 bit resolution
// #define BATTERY_SENSE_SAMPLES 15 // Set the number of samples, It has an effect of increasing sensitivity.
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER (2.0F)