#define HAS_GPS 0
#define HAS_WIRE 0
#define HAS_SCREEN 0
#define I2C_NO_RESCAN

#define UART_TX 43
#define UART_RX 44

#define LED_PAIRING 46
#define LED_LORA 46

#define LED_PIN 3
#define LED_STATE_ON 0
#define LED_STATE_OFF 1
#define BUTTON_PIN 4
#define BUTTON_ACTIVE_LOW true
#define BUTTON_ACTIVE_PULLUP true

#define LORA_SCK 11
#define LORA_MISO 9
#define LORA_MOSI 10
#define LORA_CS 12
#define LORA_RESET 39

#define USE_LR1110
#define LR1110_SPI_SCK_PIN LORA_SCK
#define LR1110_SPI_MISO_PIN LORA_MISO
#define LR1110_SPI_MOSI_PIN LORA_MOSI
#define LR1110_SPI_NSS_PIN LORA_CS
#define LR1110_IRQ_PIN 38
#define LR1110_BUSY_PIN 13
#define LR1110_NRESET_PIN LORA_RESET
#define LR11X0_DIO3_TCXO_VOLTAGE 1.8
#define LR11X0_DIO_AS_RF_SWITCH

// TEMPORARY: units shipped with LR1110 transceiver FW 0x0303 (the original 2020 release), which cannot
// reliably demodulate 500 kHz LoRa - Turbo presets fail RX with ~41% byte errors while TX and narrower
// bandwidths are fine. 0x0303 also predates GetLoRaRxHeaderInfos, which computePacketTime() calls on every
// received packet. Confirmed fixed by updating to 0x0307; targeting 0x0402 additionally picks up the
// out-of-band emission fix for consecutive LoRa transmissions (0x0401) and three CVE fixes (0x0402).
// Costs ~240 kB of flash. Remove once the affected units are updated.
#define LR11X0_UPDATE_FIRMWARE_TO 0x0402

#define HAS_ETHERNET 1
#define USE_CH390D 1

#define ETH_MISO_PIN 14
#define ETH_MOSI_PIN 48
#define ETH_SCLK_PIN 47
#define ETH_CS_PIN 21
#define ETH_INT_PIN 45
// #define ETH_ADDR 1

#define USE_ETHERNET_DEFAULT 1