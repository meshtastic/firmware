#undef I2C_SDA
#undef I2C_SCL
#define I2C_SDA 16 // I2C pins for this board
#define I2C_SCL 17

#define I2C_SDA1 45
#define I2C_SCL1 46

#define LED_PIN 6
#define LED_STATE_ON 1
#define BUTTON_PIN 0

#define HAS_TPS65233

// V1 of SubG Switch SMA 0 or F Selector 1
// #define RF_SW_SUBG1 8
// V2 of SubG Switch SMA 1 or F Selector 0
// #define RF_SW_SUBG2 5

#define RESET_OLED 8 // Emulate RF_SW_SUBG1, Use F Connector
#define VTFT_CTRL 5  // Emulate RF_SW_SUBG2, for SMA swap the pin values

#if OTHERNET_DC_REV == 2206
#define USE_LR1120

#define SPI_MISO 37
#define SPI_MOSI 39
#define SPI_SCK 38
#define SDCARD_CS 40

#define PIN_BUZZER 48

// These can either be used for GPS or a serial link. Define through Protobufs
// #define GPS_RX_PIN 10
// #define GPS_TX_PIN 21

#define PIN_POWER_EN 7            // RF section power supply enable
#define PERIPHERAL_WARMUP_MS 1000 // wait for TPS chip to initialize
#define TPS_EXTM 45               // connected, but not used
#define BIAS_T_ENABLE 9           // needs to be low
#define BIAS_T_VALUE 0
#else // 2301
#define USE_LR1121
#define SPI_MISO 10
#define SPI_MOSI 39
#define SPI_SCK 38

#define SDCARD_CS 40

// This is only informational, we always use SD cards in 1 bit mode
#define SPI_DATA1 15
#define SPI_DATA2 18

// These can either be used for GPS or a serial link. Define through Protobufs
// #define GPS_RX_PIN 36
// #define GPS_TX_PIN 37

// dac / amp instead of buzzer
#define HAS_I2S
#define DAC_I2S_BCK 21
#define DAC_I2S_WS 9
#define DAC_I2S_DOUT 48
#define DAC_I2S_MCLK 44

#define BIAS_T_ENABLE 7 // needs to be low
#define BIAS_T_VALUE 0
#define BIAS_T_SUBGHZ 2 // also needs to be low, we hijack SENSOR_POWER_CTRL_PIN to emulate this
#define SENSOR_POWER_CTRL_PIN BIAS_T_SUBGHZ
#define SENSOR_POWER_ON 0
#endif

#define HAS_SDCARD // Have SPI interface SD card slot
#define SDCARD_USE_SPI1

#define LORA_RESET 3
#define LORA_SCK 12
#define LORA_MISO 13
#define LORA_MOSI 11
#define LORA_CS 14
#define LORA_DIO9 4
#define LORA_DIO2 47

#define LR1120_IRQ_PIN LORA_DIO9
#define LR1120_NRESET_PIN LORA_RESET
#define LR1120_BUSY_PIN LORA_DIO2
#define LR1120_SPI_NSS_PIN LORA_CS
#define LR1120_SPI_SCK_PIN LORA_SCK
#define LR1120_SPI_MOSI_PIN LORA_MOSI
#define LR1120_SPI_MISO_PIN LORA_MISO

#define LR1121_IRQ_PIN LORA_DIO9
#define LR1121_NRESET_PIN LORA_RESET
#define LR1121_BUSY_PIN LORA_DIO2
#define LR1121_SPI_NSS_PIN LORA_CS
#define LR1121_SPI_SCK_PIN LORA_SCK
#define LR1121_SPI_MOSI_PIN LORA_MOSI
#define LR1121_SPI_MISO_PIN LORA_MISO

#define LR11X0_DIO3_TCXO_VOLTAGE 1.8
#define LR11X0_DIO_AS_RF_SWITCH

// This board needs external switching between sub-GHz and 2.4G circuits

// V1 of RF1 selector SubG 1 or 2.4GHz 0
// #define RF_SW_SMA1 42
// V2 of RF1 Selector SubG 0 or 2.4GHz 1
// #define RF_SW_SMA2 41

#define LR11X0_RF_SWITCH_SUBGHZ 42
#define LR11X0_RF_SWITCH_2_4GHZ 41
