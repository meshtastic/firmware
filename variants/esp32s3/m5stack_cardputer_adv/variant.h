// I2C
#define I2C_SDA SDA
#define I2C_SCL SCL

// Custom port I2C1 or UART
#define G1 1
#define G2 2

// Neopixel LED, PWR_EN Pin same as TFT Backlight GPIO38
#define HAS_NEOPIXEL                         // Enable the use of neopixels
#define NEOPIXEL_COUNT 1                     // How many neopixels are connected
#define NEOPIXEL_DATA 21                     // gpio pin used to send data to the neopixels
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800) // type of neopixels in use

// Button
#define BUTTON_PIN 0

// Battery
#define BATTERY_PIN 10
#define ADC_CHANNEL ADC1_GPIO10_CHANNEL

// IR LED
#define IR_LED 44 // Not used, information only

// TCA8418 keyboard
#define TCA8418_INT 11
// #define KB_BL_PIN -1 // No keyboard backlight
#define I2C_NO_RESCAN
#define KB_INT TCA8418_INT
#define CANNED_MESSAGE_MODULE_ENABLE 1

// GPS
#define HAS_GPS 1
#define GPS_RX_PIN RXD2
#define GPS_TX_PIN TXD2
#define GPS_BAUDRATE 115200

// BMI270
#define USE_BMI270 // INFO  | ??:??:?? 0 BMX160 found at address 0x69

// SD CARD
#define SPI_MOSI MOSI
#define SPI_SCK SCK
#define SPI_MISO MISO
#define SPI_CS 12
#define SDCARD_CS SPI_CS
#define SD_SPI_FREQUENCY 75000000U

// audio codec ES8311
#define HAS_I2S
#define DAC_I2S_BCK 41
#define DAC_I2S_WS 43
#define DAC_I2S_DOUT 46
#define DAC_I2S_DIN 42
#define DAC_I2S_MCLK -1 //???

// lovyan
#if 0
// ST7789 TFT LCD
#define ST7789_CS 37
#define ST7789_RS 34  // DC
#define ST7789_SDA 35 // MOSI
#define ST7789_SCK 36
#define ST7789_RESET 33
#define ST7789_MISO -1
#define ST7789_BUSY -1
#define ST7789_BL 38
#define ST7789_SPI_HOST SPI2_HOST
#define TFT_BL 38
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 135
#define TFT_WIDTH 240
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define TFT_OFFSET_ROTATION 2
#define SCREEN_ROTATE
#define SCREEN_TRANSITION_FRAMERATE 5 // fps
#endif

// github.com/meshtastic/st7789
#if 1
// Display (TFT)
#define USE_ST7789
#define ST7789_NSS 37
#define ST7789_RS 34  // DC
#define ST7789_SDA 35 // MOSI
#define ST7789_SCK 36
#define ST7789_RESET 33
#define ST7789_MISO -1
#define ST7789_BUSY -1
#define VTFT_CTRL 38
#define VTFT_LEDA 38
#define TFT_BACKLIGHT_ON HIGH
#define ST7789_SPI_HOST SPI2_HOST
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 135
#define TFT_WIDTH 240
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define BRIGHTNESS_DEFAULT 100 // Medium Low Brightnes
#endif

// LoRa
#define USE_SX1262 // Currently only SX1262 CAP is available
#define USE_RF95   // Test

#define LORA_SCK SCK
#define LORA_MISO MISO
#define LORA_MOSI MOSI
#define LORA_CS SS // NSS

// SX127X/RFM95
#define LORA_DIO0 4 // RF95 IRQ, SX1262 not connected
#define LORA_DIO1 6 // RF95 BUSY
#define LORA_RESET 3

// SX126X
#define SX126X_CS LORA_CS
#define SX126X_DIO1 4 // IRQ
#define SX126X_BUSY 6 // BUSY, LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define TCXO_OPTIONAL
#define SX126X_DIO3_TCXO_VOLTAGE 1.8 // Check the correct value!
