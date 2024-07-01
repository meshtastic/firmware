/*
 * Heltec HT-CT62, Heltec HT-DEV-ESP V2 and Heltec HT-DEV-ESP V3
 *
 * Heltec HT-CT62 Schematic: https://resource.heltec.cn/download/HT-CT62/HT-CT62_Schematic_Diagram.pdf
 * Heltec HT-CT62 Reference Design: https://resource.heltec.cn/download/HT-CT62/HT-CT62_Reference_Design.pdf
 * Note: Reference design is just an application note, but no actual product exists
 *
 * Heltec HT-DEV-ESP V3 Schematic: https://resource.heltec.cn/download/HT-DEV-ESP/HT-DEV-ESP_V2_Sch.pdf
 * Heltec HT-DEV-ESP V3 Schematic: https://resource.heltec.cn/download/HT-DEV-ESP/HT-DEV-ESP_V3_Sch.pdf
 * Note: Heltec sells HT-DEV-ESP boards
 *
 * Note: GPIO0 and GPIO1 are connected to 32kHz crystal on HT-DEV-ESP boards and in reference design
 *
 */

// Module does not have enough free pins for a UART GPS
#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

// USER_SW (HT-DEV-ESP V2/V3)
#define BUTTON_PIN 9

/*
 * LED (HT-DEV-ESP_V2/V3)
 * HT-CT62 reference schematic connects the LED to GPIO18 but HT-DEV-ESP schematic connects the LED to GPIO2
 */
#define LED_PIN 2
#define LED_INVERTED 0

/*
 * I2C
 * Not defined on schematics, but these are the only free GPIO exposed on the castellated pads.
 * There are pin mux conflicts: GPIO18 = USB_DN and GPIO19 = USB_DP. Since the HT-DEV-ESP provides a CP2102, re-use these ports
 * for I2C instead since it's more useful (attaching peripherals vs. connecting to a host PC). Boards which use the native ESP32
 * USB will require a separate variant (refer to Heltec HRU-3601 variant for example). Perhaps a "heltec_esp32c3_usb" variant
 * would make things easier?
 */
#define I2C_SCL 18
#define I2C_SDA 19

// Since we have I2C, probe for screens
#define HAS_SCREEN 1

// U0TXD, U0RXD to CP2102 (HT-DEV-ESP_V2/V3)
#define UART_TX 21
#define UART_RX 20

// SX1262 (HT-CT62)
#define USE_SX1262
#define LORA_SCK 10
#define LORA_MISO 6
#define LORA_MOSI 7
#define LORA_CS 8
#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET 5
#define LORA_DIO1 3
#define LORA_DIO2 RADIOLIB_NC
#define LORA_BUSY 4
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_BUSY
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
