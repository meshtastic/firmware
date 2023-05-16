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
#ifndef HAS_SENSOR
#define HAS_SENSOR 1
#endif
#ifndef HAS_RADIO
#define HAS_RADIO 1
#endif
#ifndef HAS_RTC
#define HAS_RTC 1
#endif
#ifndef HAS_CPU_SHUTDOWN
#define HAS_CPU_SHUTDOWN 1
#endif
#ifndef DEFAULT_VREF
#define DEFAULT_VREF 1100
#endif

#if defined(HAS_AXP192) || defined(HAS_AXP2101)
#define HAS_PMU
#endif
//
// set HW_VENDOR
//

// This string must exactly match the case used in release file names or the android updater won't work

#if defined(TBEAM_V10)
#define HW_VENDOR meshtastic_HardwareModel_TBEAM
#elif defined(TBEAM_V07)
#define HW_VENDOR meshtastic_HardwareModel_TBEAM_V0P7
#elif defined(LILYGO_TBEAM_S3_CORE)
#define HW_VENDOR meshtastic_HardwareModel_LILYGO_TBEAM_S3_CORE
#elif defined(DIY_V1)
#define HW_VENDOR meshtastic_HardwareModel_DIY_V1
#elif defined(RAK_11200)
#define HW_VENDOR meshtastic_HardwareModel_RAK11200
#elif defined(ARDUINO_HELTEC_WIFI_LORA_32_V2)
#ifdef HELTEC_V2_0
#define HW_VENDOR meshtastic_HardwareModel_HELTEC_V2_0
#endif
#ifdef HELTEC_V2_1
#define HW_VENDOR meshtastic_HardwareModel_HELTEC_V2_1
#endif
#elif defined(ARDUINO_HELTEC_WIFI_LORA_32)
#define HW_VENDOR meshtastic_HardwareModel_HELTEC_V1
#elif defined(TLORA_V1)
#define HW_VENDOR meshtastic_HardwareModel_TLORA_V1
#elif defined(TLORA_V2)
#define HW_VENDOR meshtastic_HardwareModel_TLORA_V2
#elif defined(TLORA_V1_3)
#define HW_VENDOR meshtastic_HardwareModel_TLORA_V1_1P3
#elif defined(TLORA_V2_1_16)
#define HW_VENDOR meshtastic_HardwareModel_TLORA_V2_1_1P6
#elif defined(TLORA_V2_1_18)
#define HW_VENDOR meshtastic_HardwareModel_TLORA_V2_1_1P8
#elif defined(GENIEBLOCKS)
#define HW_VENDOR meshtastic_HardwareModel_GENIEBLOCKS
#elif defined(PRIVATE_HW)
#define HW_VENDOR meshtastic_HardwareModel_PRIVATE_HW
#elif defined(NANO_G1)
#define HW_VENDOR meshtastic_HardwareModel_NANO_G1
#elif defined(M5STACK)
#define HW_VENDOR meshtastic_HardwareModel_M5STACK
#elif defined(STATION_G1)
#define HW_VENDOR meshtastic_HardwareModel_STATION_G1
#elif defined(DR_DEV)
#define HW_VENDOR meshtastic_HardwareModel_DR_DEV
#elif defined(HELTEC_V3)
#define HW_VENDOR meshtastic_HardwareModel_HELTEC_V3
#elif defined(HELTEC_WSL_V3)
#define HW_VENDOR meshtastic_HardwareModel_HELTEC_WSL_V3
#elif defined(TLORA_T3S3_V1)
#define HW_VENDOR meshtastic_HardwareModel_TLORA_T3_S3
#elif defined(BETAFPV_2400_TX)
#define HW_VENDOR meshtastic_HardwareModel_BETAFPV_2400_TX
#elif defined(NANO_G1_EXPLORER)
#define HW_VENDOR meshtastic_HardwareModel_NANO_G1_EXPLORER
#elif defined(BETAFPV_900_TX_NANO)
#define HW_VENDOR meshtastic_HardwareModel_BETAFPV_900_NANO_TX
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