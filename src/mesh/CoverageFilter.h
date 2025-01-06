#pragma once

#include "MeshTypes.h"
#include <array>
#include <cstddef>
#include <cstdint>

/**
 * CoverageFilter:
 * A simplified Bloom filter container designed to store coverage information,
 * such as which node IDs are "probably covered" by a packet or route.
 *
 * Here is the worst case False Postiive Rate based on the constraints defined.
 * False Positive Rate = (1-e^(-kn/m))^k
 * False Positive Rate:  k=2 (2 hash functions, 2 bits flipped), n=60 (20 nodes per hop), m=128 bits
 * False Positive Rate = 37%
 */
class CoverageFilter
{
  public:
    /**
     * Default constructor: Initialize the bit array to all zeros.
     */
    CoverageFilter();

    /**
     * Insert an item (e.g., nodeID) into the bloom filter.
     * This sets multiple bits (in this example, 2).
     * @param item: A node identifier to add to the filter.
     */
    void add(NodeNum item);

    /**
     * Check if the item might be in the bloom filter.
     * Returns true if likely present; false if definitely not present.
     * (False positives possible, false negatives are not.)
     */
    bool check(NodeNum item) const;

    /**
     * Merge (bitwise OR) another CoverageFilter into this one.
     * i.e., this->bits = this->bits OR other.bits
     */
    void merge(const CoverageFilter &other);

    /**
     * Clear all bits (optional utility).
     * This makes the filter empty again (no items).
     */
    void clear();

    /**
     * Access the underlying bits array for reading/writing,
     * e.g., if you want to store it in a packet header or Protobuf.
     */
    const std::array<uint8_t, BLOOM_FILTER_SIZE_BYTES> &getBits() const { return bits_; }
    void setBits(const std::array<uint8_t, BLOOM_FILTER_SIZE_BYTES> &newBits) { bits_ = newBits; }

  private:
    // The underlying bit array: 128 bits => 16 bytes
    std::array<uint8_t, BLOOM_FILTER_SIZE_BYTES> bits_;

    // Helper to set a bit at a given index [0..127].
    void setBit(size_t index);

    // Helper to check if a bit is set.
    bool testBit(size_t index) const;

    // Two example hash functions for demonstration.
    // TODO: consider MurmurHash, xxHash, etc.
    static size_t hash1(NodeNum value);
    static size_t hash2(NodeNum value);
};