/*
Wio-E5 mini (formerly LoRa-E5 mini)
https://www.seeedstudio.com/LoRa-E5-mini-STM32WLE5JC-p-4869.html
https://www.seeedstudio.com/LoRa-E5-Wireless-Module-p-4745.html
*/

/*
This variant is a work in progress.
Do not expect a working Meshtastic device with this target.
*/

#ifndef _VARIANT_WIOE5_
#define _VARIANT_WIOE5_

//Arduino/PlatformIO support for SUBGHZSPI is not currently available
//#define USE_SX1262

#ifdef USE_SX1262
    #define HAS_RADIO 1

    /* module only transmits through RFO_HP */
    #define SX126X_RXEN PA5
    #define SX126X_TXEN PA4

    /* TCXO fed by internal LDO option behind PB0 pin */
    #define SX126X_E22
#endif

#endif