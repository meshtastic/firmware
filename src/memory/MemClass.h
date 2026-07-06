#pragma once

// Central memory-class ladder: every RAM-sized cache in the tree keys its
// per-platform tier off MESHTASTIC_MEM_CLASS instead of growing its own chip
// #ifdef ladder. Born out of the 2.8.0 nRF52840 heap exhaustion: each cache's
// private ladder fell through to its *largest* non-PSRAM tier for any chip it
// didn't name, and nRF52 was never named. Here the unknown-chip default is the
// SMALL class - a new target boots with small caches until someone classifies
// it below, deliberately.
//
// Classes rank *usable app heap after platform overheads* (SoftDevice, WiFi+BLE
// stacks), not raw RAM. That is why classic ESP32 (520 KB raw, ~200-250 KB free
// with radios up) shares a class with nRF52840 (256 KB raw, ~115 KB arena after
// SoftDevice + FreeRTOS stacks).
//
//   MEM_CLASS_TINY    <32 KB free heap      STM32WL
//   MEM_CLASS_SMALL   ~100-250 KB           nRF52840, classic ESP32/S2/C3, RP2040
//   MEM_CLASS_MEDIUM  ~250-500 KB, no PSRAM ESP32-S3/C6/P4 without PSRAM, RP2350
//   MEM_CLASS_LARGE   PSRAM or host         ESP32-S3+PSRAM, portduino
//
// Compare ordinally: #if MESHTASTIC_MEM_CLASS >= MEM_CLASS_MEDIUM ...
//
// Overrides: a variant may predefine MESHTASTIC_MEM_CLASS (variant.h or
// -D build flag) or any individual cache constant - every consumer ladder
// stays #ifndef-guarded, so the most specific definition wins.
//
// Deliberately NOT classed here: MAX_NUM_NODES (bounded by nodes.proto fitting
// the platform's filesystem, i.e. flash-shaped, not heap-shaped) and any cache
// whose size is pinned by a hardware quirk - those branches say why inline
// (e.g. WARM_NODE_COUNT on RP2040 is watchdog-bound).
//
// This header is included very early (mesh-pb-constants.h), so it may only use
// macros that exist without configuration.h: toolchain/board -D flags
// (NRF52840_XXAA, CONFIG_IDF_TARGET_*, BOARD_HAS_PSRAM) and the ARCH_* macros
// the surrounding ladders already depend on.

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

// Compile-time ceiling for the boot-allocated mesh caches sized off this class
// (TMM cache + warm store + packet history). mesh-pb-constants.h static_asserts
// their sum against it, so the next cache-adding PR fails to build instead of
// exhausting a 115 KB arena in the field. Raising a budget is allowed - it is a
// one-line diff a reviewer can see and question.
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
