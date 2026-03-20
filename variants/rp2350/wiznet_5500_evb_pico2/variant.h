// WIZnet W5500-EVB-Pico2 variant for Meshtastic
// RP2350 + built-in W5500 Ethernet (SPI0: GP16-19, RST=GP20, INT=GP21)
// LoRa module (SX1262) must be connected externally on SPI1

// #define RADIOLIB_CUSTOM_ARDUINO 1
// #define RADIOLIB_TONE_UNSUPPORTED 1
// #define RADIOLIB_SOFTWARE_SERIAL_UNSUPPORTED 1

#define ARDUINO_ARCH_AVR

// Onboard LED (GP25)
#define LED_POWER PIN_LED

// Power monitoring
// GPIO24: VBUS sense – HIGH when USB/VBUS is present (digital, no voltage divider needed)
// GPIO29: ADC3 measures VSYS/3 (200 kΩ / 100 kΩ divider, same as standard Pico2)
#define EXT_PWR_DETECT 24  // VBUS sense; isVbusIn() reads this pin digitally
#define BATTERY_PIN    29  // VSYS/3 on ADC3 – enables Power thread & VSYS measurement
#define ADC_MULTIPLIER 3.0 // divider ratio = 3 → multiply ADC reading by 3 to get VSYS in mV
#define BATTERY_SENSE_RESOLUTION_BITS 12 // RP2350 ADC is 12-bit
// No real battery – suppress false "battery at 100%" while USB powers VSYS
#define NO_BATTERY_LEVEL_ON_CHARGE

// Optional user button — connect a button between this pin and GND
// #define BUTTON_PIN 6
// #define BUTTON_NEED_PULLUP

// GPS on UART1 (Serial2) — GP8 TX, GP9 RX
#define HAS_GPS 1
#define GPS_TX_PIN 8
#define GPS_RX_PIN 9
#define GPS_BAUDRATE 38400
#define GPS_SERIAL_PORT Serial2

// ---- EBYTE E22-900M30S on SPI1 -----------------------------------------
// Wire the module to these GPIO pins:
//   SPI1 bus : SCK=GP10  MOSI=GP11  MISO=GP12
//   CS       : GP13
//   RESET    : GP15
//   DIO1/IRQ : GP14
//   BUSY     : GP2
//   RXEN     : GP3   ← LNA enable (HIGH = RX)
//   TXEN     : ←  bridge E22_DIO2 → E22_TXEN on the module (no RP2350 GPIO needed)
//
// The E22-900M30S does NOT connect DIO2 to TXEN internally. You must bridge
// the two pins on the module (short DIO2 to TXEN with a wire or solder bridge).
// SX126X_DIO2_AS_RF_SWITCH then drives TXEN automatically during TX.
//
// HW_SPI1_DEVICE and EBYTE_E22 / EBYTE_E22_900M30S are set in platformio.ini

#define USE_SX1262
// HW_SPI1_DEVICE is set via build_flags in platformio.ini (SPI0 reserved for W5500)

#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

#define LORA_SCK  10
#define LORA_MOSI 11
#define LORA_MISO 12
#define LORA_CS   13

#define LORA_DIO0  RADIOLIB_NC
#define LORA_RESET 15
#define LORA_DIO1  14
#define LORA_DIO2  2   // BUSY pin of E22-900M30S
#define LORA_DIO3  RADIOLIB_NC

#ifdef USE_SX1262
#define SX126X_CS    LORA_CS
#define SX126X_DIO1  LORA_DIO1
#define SX126X_BUSY  LORA_DIO2
#define SX126X_RESET LORA_RESET
// GP3 = RXEN: drive HIGH at init and hold there (LNA always enabled).
// SX1262 drives DIO2 HIGH during TX (DIO2_AS_RF_SWITCH) → TXEN via bridge on module.
// Pattern matches RAK11200 (ANT_SW + DIO2_AS_RF_SWITCH, no MCU RXEN/TXEN toggling).
#define SX126X_ANT_SW 3           // GP3 = RXEN, held HIGH permanently
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8 // E22 uses TCXO controlled by DIO3
#endif

// ---- W5500 Ethernet on SPI0 --------------------------------------------
// Hardware-fixed pins on the W5500-EVB-Pico2 PCB
#define HAS_ETHERNET 1
#define WIZNET_5500_EVB_PICO2 1

// SPI0 pin assignments (wired to W5500 on the PCB)
#define ETH_SPI0_MISO 16
#define ETH_SPI0_SCK  18
#define ETH_SPI0_MOSI 19

#define PIN_ETHERNET_RESET 20  // W5500 /RST
#define PIN_ETHERNET_SS    17  // W5500 /CSn
#define ETH_SPI_PORT       SPI // SPI0 in arduino-pico
