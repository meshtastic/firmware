// E22-900M30S with ESP32-S3-WROOM-X-N4
// NOTE: ESP32-S3-WROOM-X-N4.json in boards folder (via platformio.ini board field) assumes 4 MB (Quad SPI) Flash, NO PSRAM

// For each pin in this section, provide the IO pin number of the ESP32-S3-WROOM module you connected it to
// This configuration uses the E22's DIO2 pin of the SX1262 (thereby the E22-900M30S) to control the RF switching, so we connect it to E22's TXEN
// Alternatively you may assign a pin number to E22_TXEN and it will be used for RF switching and DIO2 will not be, the DIO2 mode will be automatically set in this file!

// FIXME: We have many free pins on the ESP32-S3-WROOM-X-Y module, perhaps it is best to use one of it's pins to control TXEN, and use DIO2 as an extra interrupt?
// However, Meshtastic does not currently seem to reap any benefits from having another interrupt pin available
// Adding two 0-ohm links on your PCB design so that you can choose between the two connections would enable future software to make the most of an extra interrupt pin

// FIXME: is it best to use RADIOLIB_NC or -1, or just not define the pin at all?



//////////////////////////////////////////////////////////////////////////////
// Have custom connections or functionality? Configure them in this section //
//////////////////////////////////////////////////////////////////////////////

#define E22_NSS 14
#define E22_SCK 21
#define E22_MOSI 38
#define E22_MISO 39
#define E22_NRST 40
#define E22_BUSY 41
#define E22_DIO1 42
#define E22_RXEN 10
//#define E22_TXEN RADIOLIB_NC // E22_TXEN connected to E22_DIO2
//#define E22_DIO2 RADIOLIB_NC // E22_DIO2 connected to E22_TXEN
// External notification
// FIXME: Omitted EXT_NOTIFY_OUT as doesn't seem to have any effect
//#define EXT_NOTIFY_OUT 1 // The GPIO pin that acts as the external notification output (here we connect an LED to it)
// LED
#define LED_PIN 2
// I2C
#define I2C_SCL 18
#define I2C_SDA 8
// UART
#define UART_RX 44
#define UART_TX 43

// POWER - Output 22dBm from SX1262 for ~30dBm module output, E22-900M30S only uses last stage of the YP2233W PA
#define SX126X_MAX_POWER 22 // Defaults to 22 if not defined, but defining for good practice

// FIXME: change behavior in src to default to not having screen if is undefined
// FIXME: remove 0/1 option for HAS_SCREEN, change to being defined or not
// SCREEN - not present
#define HAS_SCREEN 0

// FIXME: change behavior in src to default to not having GPS if is undefined
// FIXME: remove 0/1 option for HAS_GPS, change to being defined or not
// GPS - not present - change behavior in src to default to not having one if is undefined
#define HAS_GPS 0



/////////////////////////////////////////////////////////////////////////////
// You should have no need to modify the code below, nor in pins_arduino.h //
/////////////////////////////////////////////////////////////////////////////

#define USE_SX1262 // E22-900M30S uses SX1262

// If E22_TXEN isn't defined or it isn't a valid pin number (like -2, -1, 49, etc.) or it's RADIOLIB_NC then assume using DIO2_AS_RF_SWITCH
// We don't use E22_DIO2 to make our decision in case this leads to using DIO2_AS_RF_SWITCH but actually have TXEN defined too causing undefined behavior or a short
// By basing our decision on E22_TXEN we are always safe, as an invalid E22_TXEN pin configuration cannot interfere with DIO2 even if it is connected as the ESP32-S3-WROOM-X-Y would never output any signal
#if (!defined(E22_TXEN) || !(0 <= E22_TXEN && E22_TXEN <= 48) || E22_TXEN == RADIOLIB_NC) // #if !(0 <= E22_TXEN && E22_TXEN <= 48) didn't work as 0xFF... trips it up
    #define DIO2_AS_RF_SWITCH
#endif

// E22-900M30S TCXO voltage is 1.8V per https://www.ebyte.com/en/pdf-down.aspx?id=781 (and https://github.com/jgromes/RadioLib/issues/12#issuecomment-520695575), so set it as such
#define DIO3_AS_TCXO_AT_1V8

#define SX126X_CS E22_NSS
#define SX126X_RESET E22_NRST
#define SX126X_BUSY E22_BUSY
#define SX126X_DIO1 E22_DIO1

#define SX126X_RXEN E22_RXEN
#define SX126X_TXEN E22_TXEN

// Even if the module is not RF95 the pins are still named as they were due to relics of the past, as in https://github.com/meshtastic/firmware/blob/8b82ae6fe3f36fbadc0dee87a82fc7e5c520a6f3/src/main.cpp#L534C8-L534C8
// FIXME: rename the constants in the file above to remove ambiguity
#define RF95_NSS E22_NSS
#define RF95_SCK E22_SCK
#define RF95_MOSI E22_MOSI
#define RF95_MISO E22_MISO

// Many of the below values would only be used if USE_RF95 was defined, but it's not as we aren't actually using an RF95, just that the 4 pins above are named like it
// If they aren't used they don't need to be defined and doing so cause confusion to those adapting this file
// LORA_RESET value is never used in src (as we are not using RF95), so no need to define
// LORA_DIO0 is not used in src (as we are not using RF95) as SX1262 does not have it per SX1262 datasheet, so no need to define
#define LORA_DIO1 E22_DIO1 // IRQ, used in (and only in) src/sleep.cpp to wake from sleep, so must define
// LORA_DIO2 value is never used in src (as we are not using RF95), so no need to define, besides if DIO2_AS_RF_SWITCH is set then it cannot serve any extra function even if requested to
// LORA_DIO3 value is never used in src (as we are not using RF95), so no need to define, besides DIO3_AS_TCXO_AT_1V8 is set so it cannot serve any extra function even if requested to
// (from 13.3.2.1 DioxMask in SX1262 datasheet: Note that if DIO2 or DIO3 are used to control the RF Switch or the TCXO, the IRQ will not be generated even if it is mapped to the pins.)
