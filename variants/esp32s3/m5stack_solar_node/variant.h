#pragma once

// M5Stack SolarNode: ESP32-S3R8, Stamp LoRa-1262 I (SX1262), ATGM336H GNSS, INA3221, M5IOE1 power switches.
// Second LoRa slot (CS 6, SPI 11/12/13) is not populated.

// I2C0: INA3221 (0x40), IP2315 USB-C charger (0x75), SHT30, SPA06
#define I2C_SDA 17
#define I2C_SCL 18
// I2C1: M5IOE1 (0x4F), BMI270 (0x68), IP2315 5VIN charger (0x75)
#define I2C_SDA1 15
#define I2C_SCL1 16

// M5IOE1 GPIO expander; bit index = IOn - 1. All enables have 100K pull-downs.
#define M5IOE1_ADDR 0x4F  // the shipped part answers here, not at the datasheet's 0x6F
#define IOE_LORA_ANT_SW 4 // IO5: Stamp LoRa SX_ANT_SW, powers the RF switch
#define IOE_GNSS_PWR_EN 7 // IO8
#define IOE_LORA_PWR_EN 9 // IO10: SY8089 LoRa 3V3 rail

#define USE_SX1262
#define LORA_SCK 38
#define LORA_MISO 39
#define LORA_MOSI 40
#define LORA_CS 5
#define LORA_DIO1 7
#define LORA_RESET 46
#define LORA_BUSY 9
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 3.0

#define HAS_GPS 1
#define GPS_RX_PIN 41
#define GPS_TX_PIN 42

#define PIN_BUZZER 47

// GPIO0 (BOOT strap, R27 10K pull-up) reaches only M12 pin 3 (G0); the M12 breakout board carries the button
#define BUTTON_PIN 0

// Common-anode RGB LED: R on GPIO44 (shared with UART0 RX), G on 48, B on 21
#define LED_LORA 44 // red: lit during TX, flashes on RX
#define LED_POWER 48
#define LED_PAIRING 21 // blue: BLE advertising / pairing / connected
#define LED_STATE_ON 0

// No ADC battery sense: battery state comes from INA3221 CH1
#define DEFAULT_BATTERY_INA_ADDRESS 0x40
// INA3221 shunts: CH1 battery, CH2 EXT battery output, CH3 solar; all 10 mOhm
#define INA3221_SHUNT_R_CH1 0.01f
#define INA3221_SHUNT_R_CH2 0.01f
#define INA3221_SHUNT_R_CH3 0.01f
// Solar charges the battery too, so INA current can't tell external power apart; charging is reported
// only while a USB-C or M12 5V input is present (see powerHAL_isVBUSConnected in variant.cpp)
#define DISABLE_INA_CHARGING_DETECTION
// IP2315 chargers answer on I2C only while their input is powered: USB-C on I2C0, M12 5VIN on I2C1
#define IP2315_ADDR 0x75
