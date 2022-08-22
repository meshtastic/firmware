#pragma once

#define ARCH_ESP32

//
// defaults for ESP32 architecture
//

#ifndef HAS_BLUETOOTH
    #define HAS_BLUETOOTH 1
#endif
#ifndef HAS_WIFI
    #define HAS_WIFI 1
#endif
#ifndef HAS_SCREEN
    #define HAS_SCREEN 1
#endif
#ifndef HAS_WIRE
    #define HAS_WIRE 1
#endif
#ifndef HAS_GPS
    #define HAS_GPS 1
#endif
#ifndef HAS_BUTTON
    #define HAS_BUTTON 1
#endif
#ifndef HAS_TELEMETRY
    #define HAS_TELEMETRY 1
#endif
#ifndef HAS_RADIO
    #define HAS_RADIO 1
#endif
#ifndef HAS_RTC
    #define HAS_RTC 1
#endif

//
// set HW_VENDOR
//

// This string must exactly match the case used in release file names or the android updater won't work

#if defined(TBEAM_V10)
    #define HW_VENDOR HardwareModel_TBEAM
#elif defined(TBEAM_V07)
    #define HW_VENDOR HardwareModel_TBEAM0p7
#elif defined(DIY_V1)
    #define HW_VENDOR HardwareModel_DIY_V1
#elif defined(RAK_11200)
    #define HW_VENDOR HardwareModel_RAK11200
#elif defined(ARDUINO_HELTEC_WIFI_LORA_32_V2)
    #ifdef HELTEC_V2_0
        #define HW_VENDOR HardwareModel_HELTEC_V2_0
    #endif
    #ifdef HELTEC_V2_1
        #define HW_VENDOR HardwareModel_HELTEC_V2_1
    #endif
#elif defined(ARDUINO_HELTEC_WIFI_LORA_32)
    #define HW_VENDOR HardwareModel_HELTEC_V1
#elif defined(TLORA_V1)
    #define HW_VENDOR HardwareModel_TLORA_V1
#elif defined(TLORA_V2)
    #define HW_VENDOR HardwareModel_TLORA_V2
#elif defined(TLORA_V1_3)
    #define HW_VENDOR HardwareModel_TLORA_V1_1p3
#elif defined(TLORA_V2_1_16)
    #define HW_VENDOR HardwareModel_TLORA_V2_1_1p6
#elif defined(GENIEBLOCKS)
    #define HW_VENDOR HardwareModel_GENIEBLOCKS
#elif defined(PRIVATE_HW)
    #define HW_VENDOR HardwareModel_PRIVATE_HW
#elif defined(NANO_G1)
    #define HW_VENDOR HardwareModel_NANO_G1
#elif defined(M5STACK)
    #define HW_VENDOR HardwareModel_M5STACK
#elif defined(STATION_G1)
    #define HW_VENDOR HardwareModel_STATION_G1
#endif

//
// Standard definitions for ESP32 targets
//

#define GPS_SERIAL_NUM 1
#ifndef GPS_RX_PIN
#define GPS_RX_PIN 34
#endif
#ifndef GPS_TX_PIN
#ifdef USE_JTAG
#define GPS_TX_PIN -1
#else
#define GPS_TX_PIN 12
#endif
#endif

// -----------------------------------------------------------------------------
// LoRa SPI
// -----------------------------------------------------------------------------

// NRF52 boards will define this in variant.h
#ifndef RF95_SCK
#define RF95_SCK 5
#define RF95_MISO 19
#define RF95_MOSI 27
#define RF95_NSS 18
#endif

#define SERIAL0_RX_GPIO 3 // Always GPIO3 on ESP32

