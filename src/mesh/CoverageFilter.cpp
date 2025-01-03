#include "CoverageFilter.h"

#include <functional> // std::hash

CoverageFilter::CoverageFilter()
{
    bits_.fill(0);
}

void CoverageFilter::add(NodeNum item)
{
    // For k=2, we just do two separate hash functions.
    size_t idx1 = hash1(item);
    size_t idx2 = hash2(item);

    setBit(idx1);
    setBit(idx2);
}

bool CoverageFilter::check(NodeNum item) const
{
    // Check both hash positions. If either bit is 0, item is definitely not in.
    size_t idx1 = hash1(item);
    if (!testBit(idx1))
        return false;

    size_t idx2 = hash2(item);
    if (!testBit(idx2))
        return false;

    // Otherwise, it might be in (false positive possible).
    return true;
}

void CoverageFilter::merge(const CoverageFilter &other)
{
    // Bitwise OR the bits.
    for (size_t i = 0; i < BLOOM_FILTER_SIZE_BYTES; i++) {
        bits_[i] |= other.bits_[i];
    }
}

void CoverageFilter::clear()
{
    bits_.fill(0);
}

// ------------------------
// Private / Helper Methods
// ------------------------

void CoverageFilter::setBit(size_t index)
{
    if (index >= BLOOM_FILTER_SIZE_BITS)
        return; // out-of-range check
    size_t byteIndex = index / 8;
    uint8_t bitMask = 1 << (index % 8);
    bits_[byteIndex] |= bitMask;
}

bool CoverageFilter::testBit(size_t index) const
{
    if (index >= BLOOM_FILTER_SIZE_BITS)
        return false;
    size_t byteIndex = index / 8;
    uint8_t bitMask = 1 << (index % 8);
    return (bits_[byteIndex] & bitMask) != 0;
}

/**
 * Very simplistic hash: combine item with a seed and use std::hash
 */
size_t CoverageFilter::hash1(NodeNum value)
{
    static const uint64_t seed1 = 0xDEADBEEF;
    uint64_t combined = value ^ (seed1 + (value << 6) + (value >> 2));

    // Use standard library hash on that combined value
    std::hash<uint64_t> hasher;
    uint64_t hashOut = hasher(combined);

    // Map to [0..127]
    return static_cast<size_t>(hashOut % BLOOM_FILTER_SIZE_BITS);
}

size_t CoverageFilter::hash2(NodeNum value)
{
    static const uint64_t seed2 = 0xBADC0FFE;
    uint64_t combined = value ^ (seed2 + (value << 5) + (value >> 3));

    std::hash<uint64_t> hasher;
    uint64_t hashOut = hasher(combined);

    return static_cast<size_t>(hashOut % BLOOM_FILTER_SIZE_BITS);
}