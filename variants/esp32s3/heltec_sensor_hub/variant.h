#define EXT_PWR_DETECT 20

#define BUTTON_PIN 17
#define BUTTON_ACTIVE_LOW false
#define BUTTON_ACTIVE_PULLUP false

#define BATTERY_PIN 7 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define ADC_CHANNEL ADC1_GPIO7_CHANNEL
#define ADC_ATTENUATION ADC_ATTEN_DB_2_5 // lower dB for high resistance voltage divider
#define ADC_MULTIPLIER (4.9 * 1.045)
#define ADC_CTRL 34 // active HIGH, powers the voltage divider. Only on 1.1
#define ADC_CTRL_ENABLED HIGH

#define HAS_NEOPIXEL                         // Enable the use of neopixels
#define NEOPIXEL_COUNT 1                     // How many neopixels are connected
#define NEOPIXEL_DATA 18                     // gpio pin used to send data to the neopixels
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800) // type of neopixels in use

#define USE_SX1262
#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET 12
#define LORA_DIO1 14 // SX1262 IRQ
#define LORA_DIO2 13 // SX1262 BUSY

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

#define I2C_SDA 1
#define I2C_SCL 2
#define HAS_SCREEN 0
#define SENSOR_POWER_CTRL_PIN 33
#define SENSOR_POWER_ON 1

#define PERIPHERAL_WARMUP_MS 100

#define ESP32S3_WAKE_TYPE ESP_EXT1_WAKEUP_ANY_HIGH

#define ENVIRONMENTAL_TELEMETRY_MODULE_ENABLE 1