/*
Board Information: https://wiki.uniteng.com/en/meshtastic/station-g2
*/

// Station G2 may not have GPS installed, but it has a GROVE GPS Socket for Optional GPS Module
#define GPS_RX_PIN 7
#define GPS_TX_PIN 15

// Station G2 has 1.3 inch OLED Screen
#define USE_SH1107_128_64

#define I2C_SDA 5 // I2C pins for this board
#define I2C_SCL 6

#define BUTTON_PIN 38 // This is the Program Button
#define BUTTON_NEED_PULLUP

#define USE_SX1262

#define LORA_MISO 14
#define LORA_SCK 12
#define LORA_MOSI 13
#define LORA_CS 11

#define LORA_RESET 21
#define LORA_DIO1 48

#ifdef USE_SX1262
#define SX126X_CS LORA_CS // FIXME - we really should define LORA_CS instead
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY 47
#define SX126X_RESET LORA_RESET

//  DIO2 controlls an antenna switch and the TCXO voltage is controlled by DIO3
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// Ensure the PA does not exceed the saturation output power. More
// Info:https://wiki.uniteng.com/en/meshtastic/station-g2#summary-for-lora-power-amplifier-conduction-test
#define SX126X_MAX_POWER 19
#endif

/*
#define BATTERY_PIN 4 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define ADC_CHANNEL ADC1_GPIO4_CHANNEL
#define ADC_MULTIPLIER 4
#define BATTERY_SENSE_SAMPLES 15 // Set the number of samples, It has an effect of increasing sensitivity.
#define BAT_FULLVOLT 8400
#define BAT_EMPTYVOLT 5000
#define BAT_CHARGINGVOLT 8400
#define BAT_NOBATVOLT 4460
#define CELL_TYPE_LION // same curve for liion/lipo
#define NUM_CELLS 2
*/
