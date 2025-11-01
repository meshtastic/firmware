#pragma once

#include "tinylsm_config.h"
#include "tinylsm_types.h"
#include <cstdint>
#include <vector>

namespace meshtastic
{
namespace tinylsm
{

// ============================================================================
// Bloom Filter (CPU-light, 2 hash functions)
// ============================================================================

class BloomFilter
{
  private:
    std::vector<uint8_t> bits;
    size_t num_bits;
    uint8_t num_hashes;
    size_t num_keys;

  public:
    BloomFilter();
    BloomFilter(size_t estimated_keys, float bits_per_key);

    // Add key to filter
    void add(CompositeKey key);

    // Check if key might be present
    bool maybe_contains(CompositeKey key) const;

    // Serialize/deserialize
    bool serialize(std::vector<uint8_t> &output) const;
    bool deserialize(const uint8_t *data, size_t size);

    // Size
    size_t size_bytes() const { return bits.size(); }
    size_t size_bits() const { return num_bits; }

  private:
    size_t hash_index(uint64_t hash, size_t idx) const;
};

} // namespace tinylsm
} // namespace meshtastic
