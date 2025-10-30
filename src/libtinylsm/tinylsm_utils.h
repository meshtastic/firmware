#pragma once

#include "tinylsm_types.h"
#include <cstddef>
#include <cstdint>

namespace meshtastic
{
namespace tinylsm
{

// ============================================================================
// CRC32 (Polynomial 0xEDB88320)
// ============================================================================

class CRC32
{
  private:
    static uint32_t table[256];
    static bool table_initialized;
    static void init_table();

  public:
    static uint32_t compute(const uint8_t *data, size_t length);
    static uint32_t compute(const uint8_t *data, size_t length, uint32_t initial);
};

// ============================================================================
// Endian Conversion (Big-endian for keys)
// ============================================================================

inline uint16_t htobe16_local(uint16_t host)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap16(host);
#else
    return host;
#endif
}

inline uint32_t htobe32_local(uint32_t host)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap32(host);
#else
    return host;
#endif
}

inline uint64_t htobe64_local(uint64_t host)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap64(host);
#else
    return host;
#endif
}

inline uint16_t be16toh_local(uint16_t big_endian)
{
    return htobe16_local(big_endian); // Same operation
}

inline uint32_t be32toh_local(uint32_t big_endian)
{
    return htobe32_local(big_endian); // Same operation
}

inline uint64_t be64toh_local(uint64_t big_endian)
{
    return htobe64_local(big_endian); // Same operation
}

// ============================================================================
// Key Encoding/Decoding
// ============================================================================

// Encode CompositeKey to big-endian bytes
inline void encode_key(CompositeKey key, uint8_t *buffer)
{
    uint64_t be_value = htobe64_local(key.value);
    memcpy(buffer, &be_value, sizeof(be_value));
}

// Decode CompositeKey from big-endian bytes
inline CompositeKey decode_key(const uint8_t *buffer)
{
    uint64_t be_value;
    memcpy(&be_value, buffer, sizeof(be_value));
    return CompositeKey(be64toh_local(be_value));
}

// ============================================================================
// Hash Functions (for Bloom filter)
// ============================================================================

// Fast 64-bit hash (splitmix64-based)
inline uint64_t hash64(uint64_t x)
{
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

// Two independent hash functions from one hash
inline void hash_bloom(CompositeKey key, uint64_t *hash1, uint64_t *hash2)
{
    uint64_t h = hash64(key.value);
    *hash1 = h;
    *hash2 = h >> 32 | (h << 32); // Rotate to get second hash
}

// ============================================================================
// Variable-length Integer Encoding (Varint for space efficiency)
// ============================================================================

// Encode uint32 as varint, return bytes written
inline size_t encode_varint32(uint32_t value, uint8_t *buffer)
{
    size_t i = 0;
    while (value >= 0x80) {
        buffer[i++] = (value & 0x7F) | 0x80;
        value >>= 7;
    }
    buffer[i++] = value & 0x7F;
    return i;
}

// Decode varint to uint32, return bytes read (0 on error)
inline size_t decode_varint32(const uint8_t *buffer, size_t max_len, uint32_t *value)
{
    *value = 0;
    size_t i = 0;
    uint32_t shift = 0;
    while (i < max_len) {
        uint8_t byte = buffer[i++];
        *value |= (static_cast<uint32_t>(byte & 0x7F) << shift);
        if ((byte & 0x80) == 0) {
            return i;
        }
        shift += 7;
        if (shift >= 32) {
            return 0; // Overflow
        }
    }
    return 0; // Incomplete varint
}

// ============================================================================
// Shard Selection
// ============================================================================

inline uint8_t select_shard(CompositeKey key, uint8_t num_shards)
{
    if (num_shards <= 1) {
        return 0;
    }
    return static_cast<uint8_t>(key.node_id() % num_shards);
}

// ============================================================================
// Time Utilities
// ============================================================================

// Get current epoch time in seconds (platform-specific, to be implemented)
uint32_t get_epoch_time();

// Check if timestamp is expired
inline bool is_expired(uint32_t timestamp, uint32_t ttl_seconds)
{
    uint32_t now = get_epoch_time();
    if (now < timestamp) {
        return false; // Clock skew, don't expire
    }
    return (now - timestamp) > ttl_seconds;
}

} // namespace tinylsm
} // namespace meshtastic
