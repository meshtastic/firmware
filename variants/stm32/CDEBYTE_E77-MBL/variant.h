/*
EByte E77-MBL series
https://www.cdebyte.com/products/E77-900MBL-01
https://www.cdebyte.com/products/E77-400MBL-01
https://github.com/olliw42/mLRS-docu/blob/master/docs/EBYTE_E77_MBL.md
*/

/*
This variant is a work in progress.
Do not expect a working Meshtastic device with this target.
*/

#ifndef _VARIANT_EBYTE_E77_
#define _VARIANT_EBYTE_E77_

#define USE_STM32WLx

#define LED_POWER PB4 // LED1
// #define LED_POWER PB3 // LED2
#define LED_STATE_ON 1

#define SERIAL_PRINT_PORT 1

#define EBYTE_E77_MBL

// LoRa
/*
 * EByte silently changed the E77-MBL hardware around early 2024: modules with serial number
 * >= 3202995 have a (better) TCXO; older modules have a ceramic crystal oscillator (XTAL) instead.
 * Since both are in the field under the same module name, treat the TCXO voltage as optional here.
 * https://github.com/olliw42/mLRS-docu/blob/main/docs/EBYTE_E77_MBL.md
 */
#define TCXO_OPTIONAL
#define SX126X_DIO3_TCXO_VOLTAGE 1.7

#endif
