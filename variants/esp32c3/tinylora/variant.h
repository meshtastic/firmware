#if defined(TINYLORA_V2)

#define BUTTON_PIN 9
#define LED_PIN 2
#define LED_STATE_ON 1

#define HAS_SCREEN 0
#define HAS_GPS 1
#undef GPS_RX_PIN 21
#undef GPS_TX_PIN 20

#define USE_LLCC68
#define USE_SX1262
#define USE_SX1268

#define LORA_SCK 10
#define LORA_MISO 6
#define LORA_MOSI 7
#define LORA_CS 8

#define LORA_DIO0 RADIOLIB_NC
#define LORA_DIO1 3
#define LORA_DIO2 RADIOLIB_NC
#define LORA_BUSY 4
#define LORA_RESET 5

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH

#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define TCXO_OPTIONAL

#elif defined(TINYLORA_V3)

#define BUTTON_PIN 9
#define LED_PIN 0
#define LED_STATE_ON 1

#define HAS_SCREEN 0
// GPS

#ifdef TINYLORA_V3_GPS
#define HAS_GPS 1
#define GPS_RX_PIN 21
#define GPS_TX_PIN 20
#define PIN_GPS_EN 2
#define GPS_EN_ACTIVE 1
#endif

#ifdef TINYLORA_V3_I2C
#define HAS_I2C 1
#define WIRE_INTERFACES_COUNT (1)
#define I2C_SDA 20
#define I2C_SCL 21
#endif

#define USE_LLCC68
#define USE_SX1262
#define USE_SX1268

#define LORA_SCK 10
#define LORA_MISO 6
#define LORA_MOSI 7
#define LORA_CS 8

#define LORA_DIO0 RADIOLIB_NC
#define LORA_DIO1 3
#define LORA_DIO2 RADIOLIB_NC
#define LORA_BUSY 4
#define LORA_RESET 5

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH

#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define TCXO_OPTIONAL

#undef  BATTERY_PIN
#define BATTERY_PIN            1            // GPIO1
#define ADC_CHANNEL            ADC1_GPIO1_CHANNEL

#undef  ADC_MULTIPLIER
#define ADC_MULTIPLIER         2.0f

#elif defined(TINYLORA_V4)

#define BUTTON_PIN 9
#define LED_PIN 0
#define LED_STATE_ON 1

#define HAS_SCREEN 0

#define USE_LLCC68
#define USE_SX1262
#define USE_SX1268

#define LORA_SCK 10
#define LORA_MISO 6
#define LORA_MOSI 7
#define LORA_CS 8

#define LORA_DIO0 RADIOLIB_NC
#define LORA_DIO1 3
#define LORA_DIO2 RADIOLIB_NC
#define LORA_BUSY 4
#define LORA_RESET 5

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_RXEN 2
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_TXEN RADIOLIB_NC

#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define TCXO_OPTIONAL

#undef  BATTERY_PIN
#define BATTERY_PIN            1            // GPIO1
#define ADC_CHANNEL            ADC1_GPIO1_CHANNEL

#undef  ADC_MULTIPLIER
#define ADC_MULTIPLIER         2.0f

#define HAS_I2C 1
#define WIRE_INTERFACES_COUNT (1)
#define I2C_SDA 20
#define I2C_SCL 21

// TinyLoRa keeps running even when the ADC reports critically low battery.
#define DISABLE_LOW_BATTERY_SHUTDOWN


#elif defined(TINYLORA_MV)

#define HAS_PCF8574_BUTTON
#define PCF8574_ADDR 0x20        // A2=A1=A0=0
#define PCF8574_INT_PIN 9

#define PCF8574_BUTTON_MAP { \
    INPUT_BROKER_SELECT,      /* P0: 确定 */ \
    INPUT_BROKER_DOWN,        /* P1: 下 */ \
    INPUT_BROKER_UP,          /* P2: 上 */ \
    INPUT_BROKER_LEFT,        /* P3: 左 */ \
    INPUT_BROKER_RIGHT,       /* P4: 右 */ \
    INPUT_BROKER_USER_PRESS,  /* P5: 用户按钮 */ \
    INPUT_BROKER_CANCEL,      /* P6: 取消 */ \
    INPUT_BROKER_NONE,        /* P7: 未使用 */ \
}

#define HAS_SCREEN 1
#define USE_SSD1306

#define HAS_I2C 1
#define WIRE_INTERFACES_COUNT (1)
#define I2C_SDA 0
#define I2C_SCL 1

#define HAS_GPS 1
#define GPS_RX_PIN 21
#define GPS_TX_PIN 20
#define GPS_POWER_TOGGLE 1
#define PIN_GPS_EN 12

#define PIN_BUZZER 5

#define USE_LLCC68
#define USE_SX1262
#define USE_SX1268

#define LORA_SCK 10
#define LORA_MISO 6
#define LORA_MOSI 7
#define LORA_CS 8

#define LORA_DIO0 RADIOLIB_NC
#define LORA_DIO1 3
#define LORA_DIO2 RADIOLIB_NC
#define LORA_BUSY 4
#define LORA_RESET 11

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_RXEN RADIOLIB_NC
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_TXEN RADIOLIB_NC

#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define TCXO_OPTIONAL

#undef  BATTERY_PIN
#define BATTERY_PIN            2
#define ADC_CHANNEL            ADC1_CHANNEL_2

#undef  ADC_MULTIPLIER
#define ADC_MULTIPLIER         2.0f

#define HAS_NEOPIXEL 1
#define NEOPIXEL_DATA 13
#define NEOPIXEL_COUNT 1 // How many neopixels are connected
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)
#define ENABLE_AMBIENTLIGHTING

#endif
