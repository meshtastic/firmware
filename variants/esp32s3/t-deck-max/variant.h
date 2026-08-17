#pragma once

#define _VARIANT_T_DECK_MAX

// Shared buses
#define I2C_SDA SDA
#define I2C_SCL SCL
#define SPI_MOSI 33
#define SPI_MISO 47
#define SPI_SCK 36

// E-Ink display
#define PIN_EINK_CS 34
#define PIN_EINK_BUSY 37
#define PIN_EINK_DC 35
#define PIN_EINK_RES 9
#define PIN_EINK_SCLK SPI_SCK
#define PIN_EINK_MOSI SPI_MOSI
#define PIN_EINK_EN 41
#define TFT_BL PIN_EINK_EN

// CST328 touch controller
#define HAS_TOUCHSCREEN 1
#define CST328_PIN_INT 12
#define CST328_PIN_RST (-1)
#define CST328_I2C_ADDR 0x1A

// Power saving and boot button
#define USE_POWERSAVE
#define SLEEP_TIME 120
#define BUTTON_PIN 0

// GPS
#define HAS_GPS 1
#define GPS_BAUDRATE 38400
#define GPS_RX_PIN 2
#define GPS_TX_PIN 16
#define PIN_GPS_PPS 1
#define PIN_GPS_EN (0x100 + BOARD_XL9555_GPS_EN)
#define GPS_EN_ACTIVE 1

// Haptic motor and sensors. The motor rail is controlled by the MAX board
// helper; do not expose its XL9555 encoding as PIN_DRV_EN to generic code.
#define HAS_DRV2605 1
#define HAS_BHI260AP 1
#define HAS_LTR553ALS 1

// TF card
#define HAS_SDCARD
#define SDCARD_USE_SPI1
#define SDCARD_CS 48
#define SPI_CS SDCARD_CS
#define SD_SPI_FREQUENCY 75000000U

// TCA8418 keyboard
#define HAS_PHYSICAL_KEYBOARD 1
#define KB_BL_PIN 42
#define KB_INT 15
#define CANNED_MESSAGE_MODULE_ENABLE 1

// ES8311 I2S audio
#define HAS_I2S
#define DAC_I2S_MCLK 38
#define DAC_I2S_BCK 39
#define DAC_I2S_WS 18
#define DAC_I2S_DOUT 17
#define DAC_I2S_DIN 40

// AudioThread controls the external amplifier through the MAX board helper.
#define AUDIO_AMP_ENABLE(on) tDeckMaxSetAmplifier(on)

// Battery charger and fuel gauge
#define HAS_PPM 1
#define XPOWERS_CHIP_SY6970
#define T_DECK_MAX_CHARGER_ADDR 0x6A
#define HAS_BQ27220 1
#define BQ27220_I2C_SDA SDA
#define BQ27220_I2C_SCL SCL
#define BQ27220_DESIGN_CAPACITY 1400

// SX1262 LoRa
#define USE_SX1262
#define USE_SX1268
#define LORA_SCK SPI_SCK
#define LORA_MISO SPI_MISO
#define LORA_MOSI SPI_MOSI
#define LORA_CS 3
#define LORA_DIO0 (-1)
#define LORA_RESET 4
#define LORA_DIO1 5
#define LORA_DIO2 6
#define LORA_DIO3
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 2.4

// XL9555 I2C expander
#define XL9555_SLAVE_ADDRESS0 0x20
#define BOARD_XL9555_MODEM_EN 0
#define BOARD_XL9555_LORA_EN 1
#define BOARD_XL9555_GPS_EN 2
#define BOARD_XL9555_IMU_EN 3
#define BOARD_XL9555_ANTENNA_SEL 4
#define BOARD_XL9555_MOTOR_EN 5
#define BOARD_XL9555_AMP_EN 6
#define BOARD_XL9555_TOUCH_RST 7
#define BOARD_XL9555_PWRKEY_EN 8
#define BOARD_XL9555_KEY_RST 9
#define BOARD_XL9555_AUDIO_SEL 10

#define MODEM_RI 7
#define MODEM_DTR 8
#define MODEM_RX 10
#define MODEM_TX 11
#define A7682_AUDIO_SERIAL_PORT Serial1
#define GPS_SERIAL_PORT Serial2
#define HAS_A7682_AUDIO 1

// Board-specific codec routing helpers used by the MAX variant.
#define T_DECK_MAX_AUDIO_ES8311 0
#define T_DECK_MAX_AUDIO_A7682E 1
