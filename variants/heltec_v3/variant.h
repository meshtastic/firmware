#define LED_PIN LED

#define HAS_GPS 0

#define RESET_OLED RST_OLED
#define I2C_SDA SDA_OLED // I2C pins for this board
#define I2C_SCL SCL_OLED

#define VEXT_ENABLE Vext // active low, powers the oled display and the lora antenna boost
#define BUTTON_PIN 0

#define BATTERY_PIN 1 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define ADC_MULTIPLIER 5.22

#define USE_SX1262

#define LORA_DIO0       -1  // a No connect on the SX1262 module
#define LORA_RESET      12
#define LORA_DIO1       14   // SX1262 IRQ
#define LORA_DIO2       13   // SX1262 BUSY
#define LORA_DIO3           // Not connected on PCB, but internally on the TTGO SX1262, if DIO3 is high the TXCO is enabled

#define RF95_SCK        9
#define RF95_MISO       11
#define RF95_MOSI       10
#define RF95_NSS        8

#define SX126X_CS       RF95_NSS
#define SX126X_DIO1     LORA_DIO1
#define SX126X_BUSY     LORA_DIO2
#define SX126X_RESET    LORA_RESET
#define SX126X_E22