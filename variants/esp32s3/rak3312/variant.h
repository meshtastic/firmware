#define I2C_SDA 9
#define I2C_SCL 40

#define USE_SX1262

#define LORA_SCK 5
#define LORA_MISO 3
#define LORA_MOSI 6
#define LORA_CS 7
#define LORA_RESET 8

#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 47
#define SX126X_BUSY 48
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif

#define LED_GREEN 46
#define LED_BLUE 45

#define PIN_LED1 LED_GREEN
#define LED_NOTIFICATION LED_BLUE

#define LED_POWER LED_GREEN
#define ledOff(pin) pinMode(pin, INPUT)

#define LED_STATE_ON 1 // State when LED is litted

#define BATTERY_PIN 1
#define ADC_CHANNEL ADC_CHANNEL_0

#ifdef _VARIANT_RAK3112_ //  Modular variant (stamp)
#define ADC_MULTIPLIER 2.11

#define BUTTON_NEED_PULLUP

#define HAS_SDCARD
#define SDCARD_USE_SPI1
#define SDCARD_CS SPI_CS

#define I2C_SDA1 PIN_WIRE1_SDA
#define I2C_SCL1 PIN_WIRE1_SCL
#else // Generic 3312 variant (40-pin standard connector)
#define ADC_MULTIPLIER 1.667

#define SX126X_POWER_EN (4)

#define PIN_POWER_EN PIN_3V3_EN
#define PIN_3V3_EN (14)

#define HAS_GPS 1
#define GPS_TX_PIN 43
#define GPS_RX_PIN 44

#ifdef RAK3312_ETH_GW
// RAK13800 Ethernet module (WIZnet W5100S-L) on the WisBlock IO slot,
// pins per RAK5005-O/RAK3312/RAK13800 datasheets.
#define HAS_ETHERNET 1
#define PIN_ETHERNET_RESET 41 // WB_IO3
#define PIN_ETHERNET_SS 12    // shared WisBlock SPI_CS
#define ETH_SCLK_PIN 13       // shared WisBlock SPI_CLK
#define ETH_MISO_PIN 10       // shared WisBlock SPI_MISO
#define ETH_MOSI_PIN 11       // shared WisBlock SPI_MOSI
// Must be a SECOND SPI peripheral, not the shared `SPI` object: that one is already
// begin()'d for the LoRa radio (pins 5/3/6/7), and ESP32 Arduino's SPIClass::begin()
// silently no-ops on a second call, so re-pinning it here would not actually move it.
#define ETH_SPI_PORT EthSPI
#endif // RAK3312_ETH_GW

#endif
