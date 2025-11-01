#include "tinylsm_filter.h"
#include "tinylsm_utils.h"
#include <cmath>
#include <cstring>

namespace meshtastic
{
namespace tinylsm
{

// ============================================================================
// BloomFilter Implementation
// ============================================================================

BloomFilter::BloomFilter() : num_bits(0), num_hashes(constants::BLOOM_NUM_HASHES), num_keys(0) {}

BloomFilter::BloomFilter(size_t estimated_keys, float bits_per_key) : num_hashes(constants::BLOOM_NUM_HASHES), num_keys(0)
{
    // Calculate optimal size
    num_bits = static_cast<size_t>(estimated_keys * bits_per_key);
    if (num_bits < 64) {
        num_bits = 64;
    }

    // Round up to byte boundary
    size_t num_bytes = (num_bits + 7) / 8;
    bits.resize(num_bytes, 0);
    num_bits = num_bytes * 8;
}

void BloomFilter::add(CompositeKey key)
{
    if (num_bits == 0) {
        return;
    }

    uint64_t h1, h2;
    hash_bloom(key, &h1, &h2);

    for (uint8_t i = 0; i < num_hashes; i++) {
        size_t bit_idx = hash_index(i == 0 ? h1 : h2, i) % num_bits;
        size_t byte_idx = bit_idx / 8;
        uint8_t bit_mask = 1 << (bit_idx % 8);
        bits[byte_idx] |= bit_mask;
    }

    num_keys++;
}

bool BloomFilter::maybe_contains(CompositeKey key) const
{
    if (num_bits == 0) {
        return true; // No filter, assume present
    }

    uint64_t h1, h2;
    hash_bloom(key, &h1, &h2);

    for (uint8_t i = 0; i < num_hashes; i++) {
        size_t bit_idx = hash_index(i == 0 ? h1 : h2, i) % num_bits;
        size_t byte_idx = bit_idx / 8;
        uint8_t bit_mask = 1 << (bit_idx % 8);

        if ((bits[byte_idx] & bit_mask) == 0) {
            return false; // Definitely not present
        }
    }

    return true; // Maybe present
}

bool BloomFilter::serialize(std::vector<uint8_t> &output) const
{
    // Format: num_bits (4B) + num_hashes (1B) + bits
    output.resize(5 + bits.size());

    uint32_t nb = num_bits;
    memcpy(output.data(), &nb, 4);
    output[4] = num_hashes;
    memcpy(output.data() + 5, bits.data(), bits.size());

    return true;
}

bool BloomFilter::deserialize(const uint8_t *data, size_t size)
{
    if (size < 5) {
        return false;
    }

    uint32_t nb;
    memcpy(&nb, data, 4);
    num_bits = nb;
    num_hashes = data[4];

    size_t expected_bytes = (num_bits + 7) / 8;
    if (size < 5 + expected_bytes) {
        return false;
    }

    bits.resize(expected_bytes);
    memcpy(bits.data(), data + 5, expected_bytes);

    return true;
}

size_t BloomFilter::hash_index(uint64_t hash, size_t idx) const
{
    // Simple hash mixing for multiple indices
    return static_cast<size_t>(hash + idx * 0x9e3779b97f4a7c15ULL);
}

} // namespace tinylsm
} // namespace meshtastic
