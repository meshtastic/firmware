// EByte EoRA-Hub
// Uses E80 (LR1121) LoRa module

#define LED_PIN 35

// Button - user interface
#define BUTTON_PIN 0 // BOOT button

#define BATTERY_PIN 1
#define ADC_CHANNEL ADC1_GPIO1_CHANNEL
#define ADC_MULTIPLIER 103.0 // Calibrated value
#define ADC_ATTENUATION ADC_ATTEN_DB_0
#define ADC_CTRL 37
#define ADC_CTRL_ENABLED LOW

// Display - OLED connected via I2C by the default hardware configuration
#define HAS_SCREEN 1
#define USE_SSD1306
#define I2C_SCL 17
#define I2C_SDA 18

// UART - The 1mm JST SH connector closest to the USB-C port
#define UART_TX 43
#define UART_RX 44

// Peripheral I2C - The 1mm JST SH connector furthest from the USB-C port which follows Adafruit connection standard. There are no
// pull-up resistors on these lines, the downstream device needs to include them. TODO: test, currently untested
#define I2C_SCL1 21
#define I2C_SDA1 10

// Radio
#define USE_LR1121

#define LORA_SCK 9
#define LORA_MOSI 10
#define LORA_MISO 11
#define LORA_RESET 12
#define LORA_CS 8
#define LORA_DIO9 13

// LR1121
#define LR1121_IRQ_PIN 14
#define LR1121_NRESET_PIN LORA_RESET
#define LR1121_BUSY_PIN LORA_DIO9
#define LR1121_SPI_NSS_PIN LORA_CS
#define LR1121_SPI_SCK_PIN LORA_SCK
#define LR1121_SPI_MOSI_PIN LORA_MOSI
#define LR1121_SPI_MISO_PIN LORA_MISO
#define LR11X0_DIO3_TCXO_VOLTAGE 1.8
#define LR11X0_DIO_AS_RF_SWITCH
