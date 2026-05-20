#pragma once

/**
 * Little-endian byte-swap utilities for mesh radio packet headers.
 *
 * On Linux native (PORTDUINO), use the system <endian.h> which handles
 * both little-endian and big-endian hosts. On all embedded targets
 * (ESP32, nRF52, STM32, RP2040) the CPU is little-endian, so these
 * are no-ops.
 *
 * Fixes: https://github.com/meshtastic/firmware/issues/6764
 */

#include <stdint.h>

#if defined(ARCH_PORTDUINO)
#include <endian.h>
static inline uint32_t meshHtoLe32(uint32_t v) { return htole32(v); }
static inline uint32_t meshLe32toH(uint32_t v) { return le32toh(v); }
static inline uint64_t meshHtoLe64(uint64_t v) { return htole64(v); }
static inline uint64_t meshLe64toH(uint64_t v) { return le64toh(v); }
#else
// All embedded Meshtastic targets are little-endian; no conversion needed.
static inline uint32_t meshHtoLe32(uint32_t v) { return v; }
static inline uint32_t meshLe32toH(uint32_t v) { return v; }
static inline uint64_t meshHtoLe64(uint64_t v) { return v; }
static inline uint64_t meshLe64toH(uint64_t v) { return v; }
#endif
