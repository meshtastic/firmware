// #define BUTTON_NEED_PULLUP // if set we need to turn on the internal CPU pullup during sleep

#define I2C_SDA1 42 // Used for PMU management and PCF8563
#define I2C_SCL1 41 // Used for PMU management and PCF8563

#define I2C_SDA 17 // For QMC6310 sensors and screens
#define I2C_SCL 18 // For QMC6310 sensors and screens

#define BUTTON_PIN 0 // The middle button GPIO on the T-Beam S3
//  #define EXT_NOTIFY_OUT 13 // Default pin to use for Ext Notify Module.

#define LED_STATE_ON 0 // State when LED is lit

// TTGO uses a common pinout for their SX1262 vs RF95 modules - both can be enabled and we will probe at runtime for RF95 and if
// not found then probe for SX1262
#define USE_SX1262
#define USE_SX1268

#define LORA_DIO0 -1 // a No connect on the SX1262 module
#define LORA_RESET 5
#define LORA_DIO1 1 // SX1262 IRQ
#define LORA_DIO2 4 // SX1262 BUSY
#define LORA_DIO3   // Not connected on PCB, but internally on the TTGO SX1262, if DIO3 is high the TXCO is enabled

#ifdef USE_SX1262
#define SX126X_CS 10 // FIXME - we really should define LORA_CS instead
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
// Not really an E22 but TTGO seems to be trying to clone that
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
// Internally the TTGO module hooks the SX1262-DIO2 in to control the TX/RX switch (which is the default for the sx1262interface
// code)
#endif

// Leave undefined to disable our PMU IRQ handler.  DO NOT ENABLE THIS because the pmuirq can cause sperious interrupts
// and waking from light sleep
// #define PMU_IRQ 40
#define HAS_AXP2101

#define HAS_RTC 1

// Specify the PMU as Wire1. In the t-beam-s3 core, PCF8563 and PMU share the bus
#define PMU_USE_WIRE1
#define RTC_USE_WIRE1

#define LORA_SCK 12
#define LORA_MISO 13
#define LORA_MOSI 11
#define LORA_CS 10

#define GPS_RX_PIN 9
#define GPS_TX_PIN 8
#define GPS_WAKEUP_PIN 7
#define GPS_1PPS_PIN 6

#define HAS_SDCARD // Have SPI interface SD card slot
#define SDCARD_USE_SPI1

// PCF8563 RTC Module
// #define PCF8563_RTC 0x51         //Putting definitions in variant. h does not compile correctly

// has 32768 Hz crystal
#define HAS_32768HZ

#define USE_SH1106