//
// WESP32, https://wesp32.com/
// ESP-32-WROOM with RTL8201FI/LAN8720 and PoE
//

#define HAS_BUTTON 0
#undef BUTTON_PIN

#undef LED_PIN

#define HAS_SCREEN 0

#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

// QWIIC connector on Revision 8
#define I2C_SDA 15
#define I2C_SCL  4

#define USE_SX1262
//
// GPIOs on JP1 connected to a Wio-SX1262 (nRF version) LoRa board
//
#define LORA_SCK 33
#define LORA_MISO 35
#define LORA_MOSI 32
#define LORA_CS 5

#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET 39
#define LORA_DIO1 18
#define LORA_BUSY 13

#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_RXEN 14
#define SX126X_CS LORA_CS
#define SX126X_TXEN RADIOLIB_NC
#define SX126X_DIO2_AS_RF_SWITCH // DIO2 is used to control the TX side of the RF switch
#define SX126X_DIO3_TCXO_VOLTAGE 1.8


#define HAS_ETHERNET 1

// Configure ESP32 RMII PHY
#define USE_ESP32_RMIIPHY
//#define ESP32_RMIIPHY_TYPE ETH_PHY_LAN8720 // Before revision 7
#define ESP32_RMIIPHY_TYPE ETH_PHY_RTL8201 // Revision 7 and later
#define ESP32_RMIIPHY_ADDR  0
#define ESP32_RMIIPHY_PWR  -1
#define ESP32_RMIIPHY_MDC  16
#define ESP32_RMIIPHY_MDIO 17
#define ESP32_RMIIPHY_CLKTYPE ETH_CLOCK_GPIO0_IN
