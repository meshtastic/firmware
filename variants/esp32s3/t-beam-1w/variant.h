// LilyGo T-Beam-1W variant.h
// Configuration based on LilyGO utilities.h and RF documentation

// I2C for OLED display (SH1106 at 0x3C)
#define I2C_SDA 8
#define I2C_SCL 9

// GPS - Quectel L76K
#define GPS_RX_PIN 5
#define GPS_TX_PIN 6
#define GPS_1PPS_PIN 7
#define GPS_WAKEUP_PIN 16 // GPS_EN_PIN in LilyGO code
#define HAS_GPS 1
#define GPS_BAUDRATE 9600

// Buttons
#define BUTTON_PIN 0      // BUTTON 1
#define ALT_BUTTON_PIN 17 // BUTTON 2

// SPI (shared by LoRa and SD)
#define SPI_MOSI 11
#define SPI_SCK 13
#define SPI_MISO 12
#define SPI_CS 10

// SD Card
#define HAS_SDCARD
#define SDCARD_USE_SPI1
#define SDCARD_CS SPI_CS

#define LORA_SCK SPI_SCK
#define LORA_MISO SPI_MISO
#define LORA_MOSI SPI_MOSI
#define LORA_CS 15
#define LORA_RESET 3
#define LORA_DIO1 1
#define LORA_BUSY 38

// The board ships with an SX1262, LR1121 or LR2021 module on the same pins; initLoRa() probes them
// in that order and keeps whichever answers.
#define USE_SX1262
#define USE_LR1121
#define USE_LR2021

// CRITICAL: Radio power enable - MUST be HIGH before lora.begin()!
// GPIO 40 powers the radio + PA module via LDO
#define SX126X_POWER_EN 40

// TX power offset for external PA (0 = no offset, full radio power). The SX1262 module's PA gain;
// the LR1121 and LR2021 modules' is about 8 dB, so they are driven slightly below their maximum.
#define TX_GAIN_LORA 10

#ifdef USE_LR1121
// LR1121 module (XY16E8P33) with sub-GHz and 2.4 GHz PAs, per LilyGO t_beam_1w_lr1121.md
#define LR1121_SPI_NSS_PIN LORA_CS
#define LR1121_IRQ_PIN 21 // radio DIO11; GPIO1 (DIO10) carries no IRQ on this module, per LilyGO utilities.h
#define LR1121_NRESET_PIN LORA_RESET
#define LR1121_BUSY_PIN LORA_BUSY
#define LR11X0_POWER_EN SX126X_POWER_EN
#define LR11X0_DIO3_TCXO_VOLTAGE 3.0
#define LR11X0_DIO_AS_RF_SWITCH // PA/LNA switching on radio DIO6-DIO8, see rfswitch.h
// The sub-GHz antenna is only reachable through the high-power PA path (external PA on DIO8).
#define LR11X0_FORCE_HP_PA
#define LR1110_MAX_POWER 22 // sub-GHz
// LilyGO: more than 1 dBm into the 2.4 GHz PA can permanently damage it.
#define LR1120_MAX_POWER 1
#endif

#ifdef USE_LR2021
// LR2021 module (XY16E3AXP33) with sub-GHz and 2.4 GHz PAs, per LilyGO t_beam_1w_lr2021.md
#define LR2021_SPI_NSS_PIN LORA_CS
#define LR2021_IRQ_PIN LORA_DIO1 // GPIO1 is radio DIO10
#define LR2021_NRESET_PIN LORA_RESET
#define LR2021_BUSY_PIN LORA_BUSY
#define LR2021_IRQ_DIO_NUM 10
// GPIO21 is radio DIO11 here (the LR1121's IRQ on that module); the SX1262 probe only claims it as RXEN after a
// successful begin(), and SX1262 boards never reach the LR1121 probe.
#define LR2021_POWER_EN SX126X_POWER_EN
#define LR2021_DIO3_TCXO_VOLTAGE 3.0f
#define LR2021_DIO_AS_RF_SWITCH // PA/LNA switching on radio DIO5-DIO8, see rfswitch.h
#define LR2021_MAX_POWER 22
// LilyGO: more than 8 dBm into the 2.4 GHz PA can permanently damage it.
#define LR2021_MAX_POWER_HF 8
#endif

#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET

// RF switching configuration for 1W PA module
// DIO2 controls PA (via SX126X_DIO2_AS_RF_SWITCH)
// CTRL PIN (GPIO 21) controls LNA - must be HIGH during RX
// Truth table: DIO2=1,CTRL=0 → TX (PA on, LNA off)
//              DIO2=0,CTRL=1 → RX (PA off, LNA on)
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_RXEN 21 // LNA enable - HIGH during RX

// TCXO voltage - required for radio init
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define SX126X_MAX_POWER 22
#endif

// LED
#define LED_POWER 18
#define LED_STATE_ON 1 // HIGH = ON

// Battery ADC
#define BATTERY_PIN 4
#define ADC_CHANNEL ADC_CHANNEL_3
#define BATTERY_SENSE_SAMPLES 30
#define ADC_MULTIPLIER 2.9333

#define OCV_ARRAY 7950, 7850, 7750, 7580, 7440, 7310, 7150, 7005, 6860, 6685, 6000

// NTC temperature sensor
#define NTC_PIN 14

// Fan control
#define FAN_CTRL_PIN 41
// Meshtastic standard fan control pin macro
#define RF95_FAN_EN FAN_CTRL_PIN

// PA Ramp Time - T-Beam 1W requires >800us stabilization (default is 200us)
// Value 0x05 = RADIOLIB_SX126X_PA_RAMP_800U
#define SX126X_PA_RAMP_US 0x05

// Display - SH1106 OLED (128x64)
#define USE_SH1106
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

// 32768 Hz crystal present
#define HAS_32768HZ 1
