#pragma once
// Seeded 64-bit LCG (Knuth MMIX constants) shared by the fuzz suites - no rand()/time(), so a failing
// iteration reproduces exactly from the printed seed. `static inline` + per-TU state suits single-file
// Unity suites; each suite keeps its own BASE_SEED/FUZZ_SEED and seeds per group.
#include <cstddef>
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
static inline void rngFill(void *buf, size_t n)
{
    uint8_t *b = (uint8_t *)buf;
    for (size_t i = 0; i < n; i++)
        b[i] = rngByte();
}
// One of the boundary NodeNums every fuzz suite should hit - 0, 1, broadcast (0xFFFFFFFF ==
// NODENUM_BROADCAST) - plus any suite-specific well-known nodes (local/remote/target) passed in.
static inline uint32_t rngEdgeNodeNum(const uint32_t *wellKnown = nullptr, size_t wellKnownN = 0)
{
    static const uint32_t edges[] = {0u, 1u, 0xFFFFFFFFu};
    const size_t numEdges = sizeof(edges) / sizeof(edges[0]);
    size_t i = rngRange((uint32_t)(numEdges + wellKnownN));
    return (i < numEdges) ? edges[i] : wellKnown[i - numEdges];
}
