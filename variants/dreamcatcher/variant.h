#undef I2C_SDA
#undef I2C_SCL
#define I2C_SDA 16 // I2C pins for this board
#define I2C_SCL 17

// #define TPS_EXTM 45 not used

#define GPS_RX_PIN 36
#define GPS_TX_PIN 37

#define LED_PIN 6
#define LED_STATE_ON 1
#define BUTTON_PIN 0
#define PIN_BUZZER 48

#define PIN_POWER_EN 7            // RF section power supply enable
#define PERIPHERAL_WARMUP_MS 1000 // wait for TPS chip to initialize
#define HAS_TPS65233

// V1 of SubG Switch SMA 0 or F Selector 1
// #define RF_SW_SUBG1 8
// V2 of SubG Switch SMA 1 or F Selector 0
// #define RF_SW_SUBG2 5

#define RESET_OLED 8 // Emulate RF_SW_SUBG1, Use F Connector
#define VTFT_CTRL 5  // Emulate RF_SW_SUBG2, for SMA swap the pin values

#define USE_LR1120
#define USE_LR1121

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

#define HAS_SDCARD // Have SPI interface SD card slot
#define SPI_MISO 10
#define SPI_MOSI 39
#define SPI_SCK 38
#define SDCARD_CS 40
#define SDCARD_USE_SPI1