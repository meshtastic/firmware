#pragma once
// ---------------------------------------------------------------------------
// Deterministic RNG for the in-tree fuzz suites - a seeded 64-bit LCG using the
// Knuth MMIX constants. No rand()/time(), so a failing fuzz iteration always
// reproduces exactly from the printed seed. Shared by test_fuzz_decode,
// test_fuzz_packets, test_hop_scaling, and test_traffic_management; each suite
// keeps its own BASE_SEED/FUZZ_SEED constant and seeds this generator per group.
//
// Functions are `static inline` so a suite that does not use rngByte() does not
// trip -Wunused-function. State (g_fuzzRng) is per translation unit, which is
// exactly what a single-file Unity suite wants.
// ---------------------------------------------------------------------------
#include <cstdint>

static uint64_t g_fuzzRng = 0;

static inline void rngSeed(uint64_t s)
{
    g_fuzzRng = s ? s : 0x9E3779B97F4A7C15ULL;
}
static inline uint32_t rngNext()
{
    g_fuzzRng = g_fuzzRng * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)(g_fuzzRng >> 32);
}
static inline uint8_t rngByte()
{
    return (uint8_t)(rngNext() & 0xFF);
}
static inline uint32_t rngRange(uint32_t n) // uniform-ish in [0, n)
{
    return n ? (rngNext() % n) : 0;
}
