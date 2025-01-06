#include "CoverageFilter.h"
#include "MeshTypes.h"

#include <RTC.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <functional>
#include <stdint.h>

/**
 * A generic Counting Coverage (bloom) filter, which can be parameterized by:
 *  - NUM_UNKNOWN_NODE_COUNTERS          (how many counter "slots")
 *  - BITS_PER_UNKNOWN_NODE_COUNTER      (4 bits, 8 bits, etc.)
 *  - BLOOM_HASH_FUNCTIONS               (number of hash functions, typically 2 or more)
 *
 */

// We have NUM_UNKNOWN_NODE_COUNTERS total "slots," and each slot is BITS_PER_UNKNOWN_NODE_COUNTER wide.
// For BITS_PER_UNKNOWN_NODE_COUNTER=4, each slot can hold 0..15.
// We store these slots in a byte array sized for the total number of bits.
// 1) Calculate how many total bits we need:
#define STORAGE_BITS NUM_UNKNOWN_NODE_COUNTERS *BITS_PER_UNKNOWN_NODE_COUNTER

// 2) Convert that to bytes (rounding up)
#define STORAGE_BYTES ((STORAGE_BITS + 7) / 8) // integer ceiling division

class CountingCoverageFilter
{
  public:
    CountingCoverageFilter();

    /**
     * Add an item (node) to this counting bloom filter.
     * Increments the counters for each hash position (up to the max for BITS_PER_COUNTER).
     */
    void add(NodeNum item);

    /**
     * Remove an item (node), decrementing counters at each hash position (if >0).
     */
    void remove(NodeNum item);

    /**
     * Check if an item "might" be in the set:
     *  - If ALL counters at those BLOOM_HASH_FUNCTIONS positions are > 0,
     *    item is "possible" (false positive possible).
     *  - If ANY position is zero, item is definitely not in the set.
     */
    bool check(NodeNum item) const;

    /**
     * Approximate count of how many items are in the filter.
     * The naive approach is sum(counters)/BLOOM_HASH_FUNCTIONS. Collisions can inflate this, though.
     */
    float approximateCount() const;

    /**
     * Merge (union) this filter with another filter of the same params.
     * We'll take the max of each counter.
     * (Alternatively you could add, but max is safer for a union.)
     */
    void merge(const CountingCoverageFilter &other);

    /**
     * Clear out all counters to zero.
     */
    void clear();

    /**
     * Compare a standard Bloom (bit-based, e.g., 16 bytes => 128 bits) to see how many bits
     * are newly set that we do not have a nonzero counter for.
     * This is purely an approximate approach for "new coverage" bits.
     */
    int approximateNewCoverageCount(const CoverageFilter &incoming) const;

    /**
     * Compare a standard Bloom (bit-based, e.g., 16 bytes => 128 bits) to see how many bits
     * are newly set that we do not have a nonzero counter for, vs. total approx. input
     * This is purely an approximate approach for "new coverage" ratio.
     */
    float approximateCoverageRatio(const CoverageFilter &incoming) const;

  private:
    uint32_t instantiationTime_;

    /**
     * The storage array, sized for all counters combined.
     * e.g. for NUM_UNKNOWN_NODE_COUNTERS=64, BITS_PER_UNKNOWN_NODE_COUNTER=4 => 64*4=256 bits => 256/8=32 bytes.
     */
    std::array<uint8_t, STORAGE_BYTES> storage_;

    /**
     * Retrieve the integer value of the counter at position idx
     * (0 <= idx < NUM_UNKNOWN_NODE_COUNTERS).
     */
    uint8_t getCounterValue(size_t idx) const;

    /**
     * Set the counter at position idx to val (clamped to max representable).
     */
    void setCounterValue(size_t idx, uint8_t val);

    /**
     * Returns true if this instance is stale (based on instantiation time).
     */
    bool isStale() const;

    /**
     * Increment the counter at idx by 1 (clamped to max).
     */
    void incrementCounter(size_t idx);

    /**
     * Decrement the counter at idx by 1 (if >0).
     */
    void decrementCounter(size_t idx);

    void computeHashIndices(NodeNum value, size_t outIndices[BLOOM_HASH_FUNCTIONS]) const;

    size_t hashGeneric(NodeNum value, uint64_t seed) const;
};