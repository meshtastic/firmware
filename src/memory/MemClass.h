#pragma once

// Central memory-class ladder: RAM-sized caches key their per-platform tier off
// MESHTASTIC_MEM_CLASS. Classes rank *usable app heap after platform overheads*
// (SoftDevice, WiFi+BLE stacks) - not raw RAM - and an unclassified chip lands in
// SMALL on purpose: small caches are a recoverable default, an exhausted heap is not.
//
//   MEM_CLASS_TINY    <32 KB free heap      STM32WL
//   MEM_CLASS_SMALL   ~100-250 KB           nRF52840, classic ESP32/S2/C3, RP2040/RP2350
//   MEM_CLASS_MEDIUM  ~250-500 KB, no PSRAM ESP32-S3/C6/P4 without PSRAM
//   MEM_CLASS_LARGE   PSRAM or host         ESP32-S3+PSRAM, portduino
//
// Compare ordinally (>=). RP2350 rides with RP2040 so this header stays a behavioral
// no-op (MEDIUM candidate later). Variants may predefine MESHTASTIC_MEM_CLASS or any
// cache constant - consumer ladders stay #ifndef-guarded. MAX_NUM_NODES is deliberately
// unclassed (flash-shaped: nodes.proto vs filesystem). Included before configuration.h,
// so only toolchain/board -D macros and the ARCH_* macros the ladders already use are
// safe here.

#define MEM_CLASS_TINY 1
#define MEM_CLASS_SMALL 2
#define MEM_CLASS_MEDIUM 3
#define MEM_CLASS_LARGE 4

#ifndef MESHTASTIC_MEM_CLASS
#if defined(ARCH_STM32WL)
#define MESHTASTIC_MEM_CLASS MEM_CLASS_TINY
#elif (defined(CONFIG_IDF_TARGET_ESP32S3) && defined(BOARD_HAS_PSRAM)) || defined(ARCH_PORTDUINO)
#define MESHTASTIC_MEM_CLASS MEM_CLASS_LARGE
#elif defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32P4)
#define MESHTASTIC_MEM_CLASS MEM_CLASS_MEDIUM
#else
// Classic ESP32 / S2 / C3, all nRF52, RP2040/RP2350, and - by design - any chip
// nobody has classified yet: small caches are a recoverable default, an
// exhausted heap is not. New RAM-rich targets opt up by adding a branch above.
#define MESHTASTIC_MEM_CLASS MEM_CLASS_SMALL
#endif
#endif // MESHTASTIC_MEM_CLASS

// Ceiling for the boot-allocated mesh caches (TMM + warm store + packet history);
// mesh-pb-constants.h static_asserts their sum, so an oversized cache fails the build.
// Raising a budget is allowed - as a visible, reviewable one-line diff.
#ifndef MESHTASTIC_BOOT_CACHE_BUDGET
#if MESHTASTIC_MEM_CLASS <= MEM_CLASS_TINY
#define MESHTASTIC_BOOT_CACHE_BUDGET (8 * 1024)
#elif MESHTASTIC_MEM_CLASS == MEM_CLASS_SMALL
#define MESHTASTIC_BOOT_CACHE_BUDGET (16 * 1024)
#elif MESHTASTIC_MEM_CLASS == MEM_CLASS_MEDIUM
#define MESHTASTIC_BOOT_CACHE_BUDGET (32 * 1024)
#else
#define MESHTASTIC_BOOT_CACHE_BUDGET (256 * 1024)
#endif
#endif // MESHTASTIC_BOOT_CACHE_BUDGET
