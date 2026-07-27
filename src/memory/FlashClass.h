#pragma once

// Central flash-class ladder: flash-sized tables key their per-platform tier off
// MESHTASTIC_FLASH_CLASS. Classes rank the *app flash region* a target actually has
// to spend - not raw part size, and deliberately not RAM.
//
//   FLASH_CLASS_TINY   <256 KB app region   STM32WL
//   FLASH_CLASS_SMALL  ~784 KB              nRF52840 (0x26000..0xEA000, warm store reserved)
//   FLASH_CLASS_MEDIUM ~1.9-3 MB app part.  classic ESP32 / S2 / C3, RP2040/RP2350
//   FLASH_CLASS_LARGE  4 MB+ app, or host   ESP32-S3 / C6 / P4, portduino
//
// This is the sibling of memory/MemClass.h, and the two ladders disagree on purpose.
// MemClass ranks usable app heap; a chip can be poor in one resource and rich in the
// other. Classic ESP32 and nRF52840 are both MEM_CLASS_SMALL, but the ESP32 has well
// over twice the app flash, so a flash-shaped decision keyed off MESHTASTIC_MEM_CLASS
// would trim the wrong targets. The reverse trap is just as real within a single class:
// rak4631 and nrf52_promicro_diy_tcxo are the same chip and the same FLASH_CLASS_SMALL,
// yet one ships with ~22 KB of headroom and the other does not fit at all - so a class
// is a *default*, never a verdict. Tight variants predefine their own value.
//
// Compare ordinally (>=). An unclassified chip lands in SMALL on purpose: a smaller
// table is a recoverable default, an image that overruns its region is not. Consumer
// ladders stay #ifndef-guarded so a variant can always override. Requires the ARCH_*
// macros, so include after (or via) configuration.h.

#define FLASH_CLASS_TINY 1
#define FLASH_CLASS_SMALL 2
#define FLASH_CLASS_MEDIUM 3
#define FLASH_CLASS_LARGE 4

#ifndef MESHTASTIC_FLASH_CLASS
#if defined(ARCH_STM32WL)
#define MESHTASTIC_FLASH_CLASS FLASH_CLASS_TINY
#elif defined(ARCH_NRF52) || defined(ARCH_NRF54L15)
#define MESHTASTIC_FLASH_CLASS FLASH_CLASS_SMALL
#elif defined(ARCH_PORTDUINO) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C6) ||                     \
    defined(CONFIG_IDF_TARGET_ESP32P4)
// S3/C6/P4 boards overwhelmingly ship 8-16 MB. The 4 MB-part S3 variants that run
// close to their app partition predefine MESHTASTIC_FLASH_CLASS instead.
#define MESHTASTIC_FLASH_CLASS FLASH_CLASS_LARGE
#elif defined(ARCH_ESP32) || defined(ARCH_RP2040)
#define MESHTASTIC_FLASH_CLASS FLASH_CLASS_MEDIUM
#else
#define MESHTASTIC_FLASH_CLASS FLASH_CLASS_SMALL
#endif
#endif // MESHTASTIC_FLASH_CLASS
