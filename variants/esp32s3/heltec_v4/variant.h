#define VEXT_ENABLE 36 // active low, powers the oled display and the lora antenna boost
#define BUTTON_PIN 0

#define ADC_CTRL 37
#define ADC_CTRL_ENABLED HIGH
#define BATTERY_PIN 1 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define ADC_CHANNEL ADC1_GPIO1_CHANNEL
#define ADC_ATTENUATION ADC_ATTEN_DB_2_5 // lower dB for high resistance voltage divider
#define ADC_MULTIPLIER 4.9 * 1.045

#define USE_SX1262

#define LORA_DIO0 -1 // a No connect on the SX1262 module
#define LORA_RESET 12
#define LORA_DIO1 14 // SX1262 IRQ
#define LORA_DIO2 13 // SX1262 BUSY
#define LORA_DIO3    // Not connected on PCB, but internally on the TTGO SX1262, if DIO3 is high the TCXO is enabled

#define LORA_SCK 9
#define LORA_MISO 11
#define LORA_MOSI 10
#define LORA_CS 8

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET

#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define USE_GC1109_PA   // We have a GC1109 power amplifier+attenuator
#define LORA_PA_POWER 7 // power en
#define LORA_PA_EN 2
#define LORA_PA_TX_EN 46 // enable tx

/*
 * GPS pins
 */
#define GPS_L76K
#define PIN_GPS_RESET (42) // An output to reset L76K GPS. As per datasheet, low for > 100ms will reset the L76K
#define GPS_RESET_MODE LOW
#define PIN_GPS_EN (34)
#define GPS_EN_ACTIVE LOW
#define PERIPHERAL_WARMUP_MS 1000 // Make sure I2C QuickLink has stable power before continuing
#define PIN_GPS_STANDBY (40)      // An output to wake GPS, low means allow sleep, high means force wake
#define PIN_GPS_PPS (41)
// Seems to be missing on this new board
#define GPS_TX_PIN (38) // This is for bits going TOWARDS the CPU
#define GPS_RX_PIN (39) // This is for bits going TOWARDS the GPS
#define GPS_THREAD_INTERVAL 50