// ST7789 TFT LCD
#define ST7789_CS 12
#define ST7789_RS 38  // DC
#define ST7789_SDA 13 // MOSI
#define ST7789_SCK 18
#define ST7789_RESET -1
#define ST7789_MISO -1
#define ST7789_BUSY -1
#define ST7789_BL 45
#define ST7789_SPI_HOST SPI3_HOST
#define TFT_BL 45
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 240
#define TFT_WIDTH 240
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define TFT_OFFSET_ROTATION 2
#define SCREEN_ROTATE
#define SCREEN_TRANSITION_FRAMERATE 5 // fps

#define HAS_TOUCHSCREEN 1
#define SCREEN_TOUCH_INT 16
#define SCREEN_TOUCH_USE_I2C1
#define TOUCH_I2C_PORT 1
#define TOUCH_SLAVE_ADDRESS 0x38

#define SLEEP_TIME 180

#define I2C_SDA1 39 // Used for capacitive touch
#define I2C_SCL1 40 // Used for capacitive touch

#define HAS_I2S
#define DAC_I2S_BCK 48
#define DAC_I2S_WS 15
#define DAC_I2S_DOUT 46
#define DAC_I2S_MCLK 0

#define HAS_AXP2101

#define HAS_RTC 1

#define I2C_SDA 10 // For QMC6310 sensors and screens
#define I2C_SCL 11 // For QMC6310 sensors and screens

#define BMA4XX_INT 14 // Interrupt for BMA_423 axis sensor

#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#define USE_SX1262
#define USE_SX1268

#define LORA_SCK 3
#define LORA_MISO 4
#define LORA_MOSI 1
#define LORA_CS 5

#define LORA_DIO0 -1 // a No connect on the SX1262 module
#define LORA_RESET 8
#define LORA_DIO1 9 // SX1262 IRQ
#define LORA_DIO2 7 // SX1262 BUSY
#define LORA_DIO3   // Not connected on PCB, but internally on the TTGO SX1262, if DIO3 is high the TXCO is enabled

#define SX126X_CS LORA_CS // FIXME - we really should define LORA_CS instead
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
// Not really an E22 but TTGO seems to be trying to clone that
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
// Internally the TTGO module hooks the SX1262-DIO2 in to control the TX/RX switch (which is the default for
// the sx1262interface code)

#define USE_VIRTUAL_KEYBOARD 1
#define DISPLAY_CLOCK_FRAME 1
