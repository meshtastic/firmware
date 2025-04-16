#define I2C_SDA 15
#define I2C_SCL 16

#if TFT_HEIGHT == 320 && not defined(HAS_TFT) // 2.4 and 2.8 TFT
// ST7789 TFT LCD
#define ST7789_CS 40
#define ST7789_RS 41  // DC
#define ST7789_SDA 39 // MOSI
#define ST7789_SCK 42
#define ST7789_RESET -1
#define ST7789_MISO 38
#define ST7789_BUSY -1
#define ST7789_BL 38
#define ST7789_SPI_HOST SPI2_HOST
#define TFT_BL 38
#define SPI_FREQUENCY 60000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_OFFSET_ROTATION 0
#define SCREEN_ROTATE
#define TFT_DUMMY_READ_PIXELS 8
#define SCREEN_TRANSITION_FRAMERATE 5
#define BRIGHTNESS_DEFAULT 130 // Medium Low Brightness

#define HAS_TOUCHSCREEN 1
#define SCREEN_TOUCH_INT 47
#define SCREEN_TOUCH_RST 48
#define TOUCH_I2C_PORT 0
#define TOUCH_SLAVE_ADDRESS 0x38 // FT5x06
#endif

#if TFT_HEIGHT == 480 && not defined(HAS_TFT) // 3.5 TFT
// ILI9488 TFT LCD
#define ILI9488_CS 40
#define ILI9488_RS 41  // DC
#define ILI9488_SDA 39 // MOSI
#define ILI9488_SCK 42
#define ILI9488_RESET -1
#define ILI9488_MISO 38
#define ILI9488_BUSY -1
#define ILI9488_BL 38
#define ILI9488_SPI_HOST SPI2_HOST
#define TFT_BL 38
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_OFFSET_ROTATION 0
#define SCREEN_ROTATE
#define TFT_DUMMY_READ_PIXELS 8
#define SCREEN_TRANSITION_FRAMERATE 5
#define BRIGHTNESS_DEFAULT 130 // Medium Low Brightness

#define HAS_TOUCHSCREEN 1
#define SCREEN_TOUCH_INT 47
#define SCREEN_TOUCH_RST 48
#define TOUCH_I2C_PORT 0
#define TOUCH_SLAVE_ADDRESS 0x5D // GT911
#endif

#ifdef CROW_SELECT
#define ST72xx_DE 42
#define ST72xx_VSYNC 41
#define ST72xx_HSYNC 40
#define ST72xx_PCLK 39
#define ST72xx_R0 7
#define ST72xx_R1 17
#define ST72xx_R2 18
#define ST72xx_R3 3
#define ST72xx_R4 46
#define ST72xx_G0 9
#define ST72xx_G1 10
#define ST72xx_G2 11
#define ST72xx_G3 12
#define ST72xx_G4 13
#define ST72xx_G5 14
#define ST72xx_B0 21
#define ST72xx_B1 47
#define ST72xx_B2 48
#define ST72xx_B3 45
#define ST72xx_B4 38

#define HAS_TOUCHSCREEN 1
#define TOUCH_I2C_PORT 0
#define TOUCH_SLAVE_ADDRESS 0x5D // GT911
#endif

#if defined(CROW_SELECT) && CROW_SELECT == 1 // 4.3 TFT 800x480
#define ST7265_HSYNC_POLARITY 0
#define ST7265_HSYNC_FRONT_PORCH 24
#define ST7265_HSYNC_PULSE_WIDTH 8
#define ST7265_HSYNC_BACK_PORCH 24
#define ST7265_VSYNC_POLARITY 1
#define ST7265_VSYNC_FRONT_PORCH 24
#define ST7265_VSYNC_PULSE_WIDTH 8
#define ST7265_VSYNC_BACK_PORCH 24
#define ST7265_PCLK_ACTIVE_NEG 1
#endif

#if defined(CROW_SELECT) && CROW_SELECT == 2 // 5.0 TFT 800x480
#define ST7262_HSYNC_POLARITY 0
#define ST7262_HSYNC_FRONT_PORCH 8
#define ST7262_HSYNC_PULSE_WIDTH 4
#define ST7262_HSYNC_BACK_PORCH 8
#define ST7262_VSYNC_POLARITY 0
#define ST7262_VSYNC_FRONT_PORCH 8
#define ST7262_VSYNC_PULSE_WIDTH 4
#define ST7262_VSYNC_BACK_PORCH 8
#define ST7262_PCLK_ACTIVE_NEG 0
#endif

#if defined(CROW_SELECT) && CROW_SELECT == 3 // 7.0 TFT 800x480
#define SC7277_HSYNC_POLARITY 0
#define SC7277_HSYNC_FRONT_PORCH 8
#define SC7277_HSYNC_PULSE_WIDTH 4
#define SC7277_HSYNC_BACK_PORCH 8
#define SC7277_VSYNC_POLARITY 0
#define SC7277_VSYNC_FRONT_PORCH 8
#define SC7277_VSYNC_PULSE_WIDTH 4
#define SC7277_VSYNC_BACK_PORCH 8
#define SC7277_PCLK_ACTIVE_NEG 0
#endif

#if TFT_HEIGHT == 320 // 2.4-2.8 have I2S audio
// dac / amp
// #define HAS_I2S // didn't get I2S sound working
#define PIN_BUZZER 8 // using pwm buzzer instead (nobody will notice, lol)
#define DAC_I2S_BCK 13
#define DAC_I2S_WS 11
#define DAC_I2S_DOUT 12
#define DAC_I2S_MCLK 8 // don't use GPIO0 because it's assigned to LoRa or button
#else
#define PIN_BUZZER 8
#endif

// GPS via UART1 connector
#define HAS_GPS 1
#define GPS_DEFAULT_NOT_PRESENT 1
#define GPS_RX_PIN 18
#define GPS_TX_PIN 17

// Extension Slot Layout, viewed from above (2.4-3.5)
// DIO1/IO1 o   o IO2/NRESET
// SCK/IO10 o   o IO16/NC
// MISO/IO9 o   o IO15/NC
// MOSI/IO3 o   o NC/DIO2
//      3V3 o   o IO46/BUSY
//      GND o   o IO0/NSS
//    5V/NC o   o NC/DIO3
//         J9   J8

// Extension Slot Layout, viewed from above (4.3-7.0)
// !! DIO1/IO20 o   o IO19/NRESET !!
// !!   SCK/IO5 o   o IO16/NC
// !!  MISO/IO4 o   o IO15/NC
// !!  MOSI/IO6 o   o NC/DIO2
//          3V3 o   o IO2/BUSY !!
//          GND o   o IO0/NSS
//        5V/NC o   o NC/DIO3
//             J9   J8

// LoRa
#define USE_SX1262
#define LORA_CS 0 // GND

#if TFT_HEIGHT == 320 || TFT_HEIGHT == 480 // 2.4 - 3.5 TFT
#define LORA_SCK 10
#define LORA_MISO 9
#define LORA_MOSI 3

#define LORA_RESET 2
#define LORA_DIO1 1  // SX1262 IRQ
#define LORA_DIO2 46 // SX1262 BUSY

// need to pull IO45 low to enable LORA and disable Microphone on 24 28 35
#define SENSOR_POWER_CTRL_PIN 45
#define SENSOR_POWER_ON LOW
#else
#define LORA_SCK 5
#define LORA_MISO 4
#define LORA_MOSI 6

#define LORA_RESET 19
#define LORA_DIO1 20 // SX1262 IRQ
#define LORA_DIO2 2  // SX1262 BUSY
#endif

#define HW_SPI1_DEVICE
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH

#define SX126X_DIO3_TCXO_VOLTAGE 3.3

#define USE_VIRTUAL_KEYBOARD 1
#define DISPLAY_CLOCK_FRAME 1