#pragma once

#include "CountingCoverageFilter.h"

CountingCoverageFilter::CountingCoverageFilter()
{
    clear();
    instantiationTime_ = getTime();
}

/**
 * Add an item (node) to this counting bloom filter.
 * Increments the counters for each hash position (up to the max for BITS_PER_COUNTER).
 */
void CountingCoverageFilter::add(NodeNum item)
{
    // We'll do BLOOM_HASH_FUNCTIONS hash functions. Typically BLOOM_HASH_FUNCTIONS=2 for simplicity.
    size_t indices[BLOOM_HASH_FUNCTIONS];
    computeHashIndices(item, indices);

    for (size_t i = 0; i < BLOOM_HASH_FUNCTIONS; i++) {
        incrementCounter(indices[i]);
    }
}

/**
 * Remove an item (node), decrementing counters at each hash position (if >0).
 */
void CountingCoverageFilter::remove(NodeNum item)
{
    size_t indices[BLOOM_HASH_FUNCTIONS];
    computeHashIndices(item, indices);

    for (size_t i = 0; i < BLOOM_HASH_FUNCTIONS; i++) {
        decrementCounter(indices[i]);
    }
}

/**
 * Check if an item "might" be in the set:
 *  - If ALL counters at those BLOOM_HASH_FUNCTIONS positions are > 0,
 *    item is "possible" (false positive possible).
 *  - If ANY position is zero, item is definitely not in the set.
 */
bool CountingCoverageFilter::check(NodeNum item) const
{
    size_t indices[BLOOM_HASH_FUNCTIONS];
    computeHashIndices(item, indices);

    for (size_t i = 0; i < BLOOM_HASH_FUNCTIONS; i++) {
        if (getCounterValue(indices[i]) == 0) {
            return false; // definitely not in
        }
    }
    return true; // might be in
}

/**
 * Approximate count of how many items are in the filter.
 * The naive approach is sum(counters)/BLOOM_HASH_FUNCTIONS. Collisions can inflate this, though.
 */
float CountingCoverageFilter::approximateCount() const
{
    uint64_t sum = 0;
    for (size_t i = 0; i < NUM_UNKNOWN_NODE_COUNTERS; i++) {
        sum += getCounterValue(i);
    }
    // We do K increments per item, so a naive estimate is sum/BLOOM_HASH_FUNCTIONS
    return static_cast<float>(sum) / static_cast<float>(BLOOM_HASH_FUNCTIONS);
}

/**
 * Merge (union) this filter with another filter of the same params.
 * We'll take the max of each counter.
 * (Alternatively you could add, but max is safer for a union.)
 */
void CountingCoverageFilter::merge(const CountingCoverageFilter &other)
{
    for (size_t i = 0; i < NUM_UNKNOWN_NODE_COUNTERS; i++) {
        uint8_t mine = getCounterValue(i);
        uint8_t theirs = other.getCounterValue(i);
        uint8_t mergedVal = (mine > theirs) ? mine : theirs;
        setCounterValue(i, mergedVal);
    }
}

/**
 * Clear out all counters to zero.
 */
void CountingCoverageFilter::clear()
{
    storage_.fill(0);
}

/**
 * Compare a standard Bloom (bit-based, e.g., 16 bytes => 128 bits) to see how many bits
 * are newly set that we do not have a nonzero counter for.
 * This is purely an approximate approach for "new coverage" bits.
 */
int CountingCoverageFilter::approximateNewCoverageCount(const CoverageFilter &incoming) const
{
    if (isStale())
        return 0.0f;

    // 1) Retrieve the bits from the incoming coverage filter
    const auto &bits = incoming.getBits();  // this is a std::array<uint8_t, BLOOM_FILTER_SIZE_BYTES>
    size_t coverageByteCount = bits.size(); // typically 16 bytes => 128 bits

    size_t maxBitsToCheck = coverageByteCount * 8;
    if (maxBitsToCheck > NUM_UNKNOWN_NODE_COUNTERS) {
        maxBitsToCheck = NUM_UNKNOWN_NODE_COUNTERS;
    }

    int newCoverageBits = 0;
    for (size_t bitIndex = 0; bitIndex < maxBitsToCheck; bitIndex++) {
        size_t byteIndex = bitIndex / 8;
        uint8_t bitMask = 1 << (bitIndex % 8);

        // Was this bit set in the incoming coverage filter?
        bool coverageBitSet = (bits[byteIndex] & bitMask) != 0;
        if (!coverageBitSet) {
            continue;
        }

        // If our local counter at bitIndex is 0 => "new coverage" bit
        if (getCounterValue(bitIndex) == 0) {
            newCoverageBits++;
        }
    }
    return newCoverageBits;
}

float CountingCoverageFilter::approximateCoverageRatio(const CoverageFilter &incoming) const
{
    if (isStale())
        return 0.0f;

    // 1) How many "new coverage" bits do we see?
    int newBits = approximateNewCoverageCount(incoming);

    // 2) How many items do we hold, approx?
    float myApproxCount = approximateCount();
    if (myApproxCount < 0.00001f) {
        // Avoid division by zero; or you can return 0 or 1 as suits your logic.
        return 0.0f;
    }

    // newBits is a bit count, approximateCount() is an item count.
    // This is a rough ratio. Decide if you want them in the same domain.
    // We'll treat "newBits" ~ "new items," so ratio = newBits / myApproxCount
    return static_cast<float>(newBits) / myApproxCount;
}

uint8_t CountingCoverageFilter::getCounterValue(size_t idx) const
{
    assert(idx < NUM_UNKNOWN_NODE_COUNTERS);
    if (BITS_PER_UNKNOWN_NODE_COUNTER == 8) {
        // Easiest case: 1 byte per counter
        return storage_[idx];
    } else if (BITS_PER_UNKNOWN_NODE_COUNTER == 4) {
        // 2 counters per byte
        size_t byteIndex = idx / 2;   // each byte holds 2 counters
        bool second = (idx % 2) == 1; // 0 => lower nibble, 1 => upper nibble
        uint8_t rawByte = storage_[byteIndex];
        if (!second) {
            // lower 4 bits
            return (rawByte & 0x0F);
        } else {
            // upper 4 bits
            return (rawByte >> 4) & 0x0F;
        }
    } else {
        // If you want to handle other bit widths (2, 3, 16, etc.), you'd do more logic here.
        static_assert(BITS_PER_UNKNOWN_NODE_COUNTER == 4 || BITS_PER_UNKNOWN_NODE_COUNTER == 8,
                      "Only 4-bit or 8-bit counters allowed.");
        return 0;
    }
}

/**
 * Set the counter at position idx to val (clamped to max representable).
 */
void CountingCoverageFilter::setCounterValue(size_t idx, uint8_t val)
{
    assert(idx < NUM_UNKNOWN_NODE_COUNTERS);
    // clamp val
    uint8_t maxVal = (1 << BITS_PER_UNKNOWN_NODE_COUNTER) - 1; // e.g. 15 for 4 bits, 255 for 8 bits
    if (val > maxVal)
        val = maxVal;

    if (BITS_PER_UNKNOWN_NODE_COUNTER == 8) {
        storage_[idx] = val;
    } else if (BITS_PER_UNKNOWN_NODE_COUNTER == 4) {
        size_t byteIndex = idx / 2;
        bool second = (idx % 2) == 1;
        uint8_t rawByte = storage_[byteIndex];

        if (!second) {
            // Lower nibble
            // clear lower nibble, then set
            rawByte = (rawByte & 0xF0) | (val & 0x0F);
        } else {
            // Upper nibble
            // clear upper nibble, then set
            rawByte = (rawByte & 0x0F) | ((val & 0x0F) << 4);
        }
        storage_[byteIndex] = rawByte;
    }
}

bool CountingCoverageFilter::isStale() const
{
    // How long has it been since this filter was created?
    uint32_t now = getTime();
    uint32_t age = now - instantiationTime_;
    return age > STALE_COVERAGE_SECONDS;
}

/**
 * Increment the counter at idx by 1 (clamped to max).
 */
void CountingCoverageFilter::incrementCounter(size_t idx)
{
    // read current
    uint8_t currVal = getCounterValue(idx);
    // increment
    uint8_t nextVal = currVal + 1; // might overflow if at max
    setCounterValue(idx, nextVal);
}

/**
 * Decrement the counter at idx by 1 (if >0).
 */
void CountingCoverageFilter::decrementCounter(size_t idx)
{
    // read current
    uint8_t currVal = getCounterValue(idx);
    if (currVal > 0) {
        setCounterValue(idx, currVal - 1);
    }
    // else do nothing (can't go negative)
}

void CountingCoverageFilter::computeHashIndices(NodeNum value, size_t outIndices[BLOOM_HASH_FUNCTIONS]) const
{
    // We can use two or more seeds for separate hashes. Here we do two seeds as an example.
    // If BLOOM_HASH_FUNCTIONS > 2, you'd do more seeds or vary the combined approach.
    static const uint64_t seed1 = 0xDEADBEEF;
    static const uint64_t seed2 = 0xBADC0FFE;

    outIndices[0] = hashGeneric(value, seed1);
    if (BLOOM_HASH_FUNCTIONS >= 2) {
        outIndices[1] = hashGeneric(value, seed2);
    }
    // If BLOOM_HASH_FUNCTIONS were greater than 2, we'd have to update similarly for outIndices[2],
    // outIndices[3], etc. with new seeds
}

size_t CountingCoverageFilter::hashGeneric(NodeNum value, uint64_t seed) const
{
    // Just a simplistic combine of "value" and "seed" then do std::hash<uint64_t>.
    uint64_t combined = value ^ (seed + (value << 6) + (value >> 2));

    std::hash<uint64_t> hasher;
    uint64_t hashOut = hasher(combined);

    // Then map to [0..(NUM_UNKNOWN_NODE_COUNTERS-1)]
    // because each "slot" is an index from 0..NUM_UNKNOWN_NODE_COUNTERS-1
    return static_cast<size_t>(hashOut % NUM_UNKNOWN_NODE_COUNTERS);
}