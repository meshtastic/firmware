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
#ifndef HAS_CUSTOM_CRYPTO_ENGINE
#define HAS_CUSTOM_CRYPTO_ENGINE 1
#endif

#if defined(HAS_AXP192) || defined(HAS_AXP2101)
#define HAS_PMU
#endif

#ifdef PIN_BUTTON_TOUCH
#define BUTTON_PIN_TOUCH PIN_BUTTON_TOUCH
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
#elif defined(HELTEC_WIRELESS_BRIDGE)
#define HW_VENDOR meshtastic_HardwareModel_HELTEC_WIRELESS_BRIDGE
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
#elif defined(TLORA_C6)
#define HW_VENDOR meshtastic_HardwareModel_TLORA_C6
#elif defined(T_DECK)
#define HW_VENDOR meshtastic_HardwareModel_T_DECK
#elif defined(T_WATCH_S3)
#define HW_VENDOR meshtastic_HardwareModel_T_WATCH_S3
#elif defined(GENIEBLOCKS)
#define HW_VENDOR meshtastic_HardwareModel_GENIEBLOCKS
#elif defined(PRIVATE_HW)
#define HW_VENDOR meshtastic_HardwareModel_PRIVATE_HW
#elif defined(NANO_G1)
#define HW_VENDOR meshtastic_HardwareModel_NANO_G1
#elif defined(M5STACK)
#define HW_VENDOR meshtastic_HardwareModel_M5STACK
#elif defined(M5STACK_CORES3)
#define HW_VENDOR meshtastic_HardwareModel_M5STACK_CORES3
#elif defined(STATION_G1)
#define HW_VENDOR meshtastic_HardwareModel_STATION_G1
#elif defined(DR_DEV)
#define HW_VENDOR meshtastic_HardwareModel_DR_DEV
#elif defined(HELTEC_HRU_3601)
#define HW_VENDOR meshtastic_HardwareModel_HELTEC_HRU_3601
#elif defined(HELTEC_V3)
#define HW_VENDOR meshtastic_HardwareModel_HELTEC_V3
#elif defined(HELTEC_WSL_V3)
#define HW_VENDOR meshtastic_HardwareModel_HELTEC_WSL_V3
#elif defined(HELTEC_WIRELESS_TRACKER)
#ifdef HELTEC_TRACKER_V1_0
#define HW_VENDOR meshtastic_HardwareModel_HELTEC_WIRELESS_TRACKER_V1_0
#else
#define HW_VENDOR meshtastic_HardwareModel_HELTEC_WIRELESS_TRACKER
#endif
#elif defined(HELTEC_WIRELESS_PAPER_V1_0)
#define HW_VENDOR meshtastic_HardwareModel_HELTEC_WIRELESS_PAPER_V1_0
#elif defined(HELTEC_WIRELESS_PAPER)
#define HW_VENDOR meshtastic_HardwareModel_HELTEC_WIRELESS_PAPER
#elif defined(TLORA_T3S3_V1)
#define HW_VENDOR meshtastic_HardwareModel_TLORA_T3_S3
#elif defined(TLORA_T3S3_EPAPER)
#define HW_VENDOR meshtastic_HardwareModel_TLORA_T3_S3
#elif defined(CDEBYTE_EORA_S3)
#define HW_VENDOR meshtastic_HardwareModel_CDEBYTE_EORA_S3
#elif defined(BETAFPV_2400_TX)
#define HW_VENDOR meshtastic_HardwareModel_BETAFPV_2400_TX
#elif defined(NANO_G1_EXPLORER)
#define HW_VENDOR meshtastic_HardwareModel_NANO_G1_EXPLORER
#elif defined(BETAFPV_900_TX_NANO)
#define HW_VENDOR meshtastic_HardwareModel_BETAFPV_900_NANO_TX
#elif defined(PICOMPUTER_S3)
#define HW_VENDOR meshtastic_HardwareModel_PICOMPUTER_S3
#elif defined(HELTEC_HT62)
#define HW_VENDOR meshtastic_HardwareModel_HELTEC_HT62
#elif defined(EBYTE_ESP32_S3)
#define HW_VENDOR meshtastic_HardwareModel_EBYTE_ESP32_S3
#elif defined(ELECROW_ThinkNode_M2)
#define HW_VENDOR meshtastic_HardwareModel_THINKNODE_M2
#elif defined(ESP32_S3_PICO)
#define HW_VENDOR meshtastic_HardwareModel_ESP32_S3_PICO
#elif defined(SENSELORA_S3)
#define HW_VENDOR meshtastic_HardwareModel_SENSELORA_S3
#elif defined(HELTEC_HT62)
#define HW_VENDOR meshtastic_HardwareModel_HELTEC_HT62
#elif defined(CHATTER_2)
#define HW_VENDOR meshtastic_HardwareModel_CHATTER_2
#elif defined(STATION_G2)
#define HW_VENDOR meshtastic_HardwareModel_STATION_G2
#elif defined(UNPHONE)
#define HW_VENDOR meshtastic_HardwareModel_UNPHONE
#elif defined(WIPHONE)
#define HW_VENDOR meshtastic_HardwareModel_WIPHONE
#elif defined(RADIOMASTER_900_BANDIT_NANO)
#define HW_VENDOR meshtastic_HardwareModel_RADIOMASTER_900_BANDIT_NANO
#elif defined(RADIOMASTER_900_BANDIT)
#define HW_VENDOR meshtastic_HardwareModel_RADIOMASTER_900_BANDIT
#elif defined(HELTEC_CAPSULE_SENSOR_V3)
#define HW_VENDOR meshtastic_HardwareModel_HELTEC_CAPSULE_SENSOR_V3
#elif defined(HELTEC_VISION_MASTER_T190)
#define HW_VENDOR meshtastic_HardwareModel_HELTEC_VISION_MASTER_T190
#elif defined(HELTEC_VISION_MASTER_E213)
#define HW_VENDOR meshtastic_HardwareModel_HELTEC_VISION_MASTER_E213
#elif defined(HELTEC_VISION_MASTER_E290)
#define HW_VENDOR meshtastic_HardwareModel_HELTEC_VISION_MASTER_E290
#elif defined(SENSECAP_INDICATOR)
#define HW_VENDOR meshtastic_HardwareModel_SENSECAP_INDICATOR
#elif defined(SEEED_XIAO_S3)
#define HW_VENDOR meshtastic_HardwareModel_SEEED_XIAO_S3
#elif defined(MESH_TAB)
#define HW_VENDOR meshtastic_HardwareModel_MESH_TAB
#elif defined(T_ETH_ELITE)
#define HW_VENDOR meshtastic_HardwareModel_T_ETH_ELITE
#elif defined(HELTEC_SENSOR_HUB)
#define HW_VENDOR meshtastic_HardwareModel_HELTEC_SENSOR_HUB
#elif defined(ELECROW_PANEL)
#define HW_VENDOR meshtastic_HardwareModel_CROWPANEL
#elif defined(LINK_32)
#define HW_VENDOR meshtastic_HardwareModel_LINK_32
#endif

// -----------------------------------------------------------------------------
// LoRa SPI
// -----------------------------------------------------------------------------

// If an SPI-related pin used by the LoRa module isn't defined, use the conventional pin number for it.
// FIXME: these pins should really be defined in each variant.h file to prevent breakages if the defaults change, currently many
// ESP32 variants don't define these pins in their variant.h file.
#ifndef LORA_SCK
#define LORA_SCK 5
#endif
#ifndef LORA_MISO
#define LORA_MISO 19
#endif
#ifndef LORA_MOSI
#define LORA_MOSI 27
#endif
#ifndef LORA_CS
#define LORA_CS 18
#endif

#define SERIAL0_RX_GPIO 3 // Always GPIO3 on ESP32 // FIXME: may be different on ESP32-S3, etc.
