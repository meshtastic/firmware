#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

/**
 * CompactHistogram: Bit-packed histogram for tracking node hop distances over time.
 *
 * Memory layout: 512 bytes total (128 nodes × 4 bytes/entry)
 * - 13-bit node ID + 3-bit hop count per entry
 * - 16-bit time window bitmap (4 short-term 30-min + 12 long-term 4-hour windows)
 *
 * Design principles:
 * - Pure counter, not a database
 * - Fixed memory footprint regardless of mesh size
 * - Bitwise modulo sampling for high-traffic scenarios
 * - Adaptive denominator scaling based on histogram fullness
 */

struct HistogramEntry {
    uint16_t nodeIdAndHops; // 13-bit node ID (bits 0–12) + 3-bit hop count (bits 13–15)
    uint16_t windowBitmap;  // 4-bit short-term (bits 0–3) + 12-bit long-term (bits 4–15)
};

class CompactHistogram
{
  public:
    // Capacity and memory layout
    static constexpr size_t CAPACITY = 128;
    static constexpr size_t ENTRY_SIZE = sizeof(HistogramEntry);
    static constexpr size_t TOTAL_SIZE = CAPACITY * ENTRY_SIZE; // 512 bytes

    // Window definitions
    static constexpr uint8_t SHORT_TERM_WINDOWS = 4; // 30-minute granularity
    static constexpr uint8_t LONG_TERM_WINDOWS = 12; // 4-hour granularity
    static constexpr uint8_t TOTAL_WINDOWS = SHORT_TERM_WINDOWS + LONG_TERM_WINDOWS;

    static constexpr uint32_t SHORT_WINDOW_DURATION_MS = 30 * 60 * 1000;    // 30 minutes
    static constexpr uint32_t LONG_WINDOW_DURATION_MS = 4 * 60 * 60 * 1000; // 4 hours

    // Sampling denominator limits (power-of-2 only)
    static constexpr uint8_t SAMPLING_DENOMINATOR_MIN = 1;
    static constexpr uint8_t SAMPLING_DENOMINATOR_MAX = 128;
    static constexpr uint8_t SAMPLING_DENOMINATOR_INITIAL = 8;

    // Maximum hop value (3 bits = 0–7)
    static constexpr uint8_t MAX_HOP = 7;

    CompactHistogram();
    ~CompactHistogram() = default;

    // --- Sampling & Entry Management ---

    /// Sample an incoming packet. Updates or creates entry for the given node ID.
    /// Uses bitwise modulo sampling: only processes if (nodeId & (denominator - 1)) == jitterOffset
    void sampleRxPacket(uint32_t nodeId, uint8_t hopCount);

    /// Clear all entries and reset histogram state. Re-randomizes jitter offset.
    void clear();

    /// Get current entry count (nodes tracked in histogram)
    uint8_t getEntryCount() const { return count; }

    /// Get histogram fill percentage (0–100)
    uint8_t getFillPercentage() const { return (count * 100) / CAPACITY; }

    // --- Window Management ---

    /// Check if a window roll is due (30 minutes elapsed since last roll)
    bool isWindowRollDue() const;

    /// Perform window rolling: aggregate expiring short-term window into long-term, shift buffers
    void rollWindows();

    /// Update the current window index based on elapsed time
    void updateCurrentWindowIndex();

    // --- Hop Distribution Query ---

    struct HopDistribution {
        uint8_t minHops = 0;
        uint8_t maxHops = 0;
        uint8_t medianHops = 0;
        uint8_t percentile25Hops = 0;
        uint8_t percentile75Hops = 0;
        size_t sampleCount = 0;
    };

    struct PerHopDistribution {
        uint16_t perHop[MAX_HOP + 1];
        size_t totalSamples = 0;
    };

    struct MeshSizeEstimate {
        uint16_t estimatedNodes = 0;
        uint16_t lowerBoundNodes = 0;
        uint16_t sampledNodes = 0;
        uint8_t denominator = SAMPLING_DENOMINATOR_INITIAL;
        uint8_t confidencePercent = 0;
        bool saturated = false;
        bool lowerBoundOnly = false;
    };

    /// Get hop distribution from histogram entries.
    /// If recentOnly=true, only count entries seen in the last 2 hours (short-term windows).
    HopDistribution getHopDistribution(bool recentOnly = false) const;

    /// Get per-hop node counts from histogram entries.
    /// If recentOnly=true, only count entries seen in the last 2 hours (short-term windows).
    PerHopDistribution getPerHopDistribution(bool recentOnly = false) const;

    /// Estimate total mesh size from sampled histogram population.
    /// If saturated, this estimate is reported as a lower-bound with degraded confidence.
    MeshSizeEstimate estimateMeshSize(bool recentOnly = true) const;

    // --- Adaptive Denominator Scaling ---

    /// Get current sampling denominator
    uint8_t getSamplingDenominator() const { return samplingDenominator; }

    /// Set sampling denominator (must be power-of-2, clamped to [MIN, MAX])
    void setSamplingDenominator(uint8_t denominator);

    /// Check if denominator should be scaled up (histogram nearly full)
    bool shouldScaleUpDenominator() const;

    /// Check if denominator should be scaled down (histogram sparse)
    bool shouldScaleDownDenominator() const;

    /// Increment step-up event counter
    void recordStepUpEvent() { stepUpEvents++; }

    /// Increment step-down event counter
    void recordStepDownEvent() { stepDownEvents++; }

    uint8_t getStepUpEvents() const { return stepUpEvents; }
    uint8_t getStepDownEvents() const { return stepDownEvents; }

    // --- Debugging & Inspection ---

    /// Get jitter offset (for testing)
    uint8_t getJitterOffset() const { return jitterOffset; }

    /// Get last window roll time (for testing)
    uint32_t getLastWindowRollTime() const { return lastWindowRollTime; }

    /// Get current window index (for testing)
    uint8_t getCurrentWindowIndex() const { return currentWindowIndex; }

    /// Get session start time (for testing)
    uint32_t getSessionStartTime() const { return sessionStartTime; }

#ifdef UNIT_TEST
    /// Override internal clock for deterministic unit tests.
    static void setTimeForTest(uint32_t nowMs);
    static void clearTimeForTest();
#endif

  private:
    // --- Data Storage ---
    HistogramEntry entries[CAPACITY];
    uint8_t count = 0; // Current number of tracked nodes

    // --- Window Management ---
    uint32_t sessionStartTime = 0;   // Timestamp when histogram was initialized
    uint32_t lastWindowRollTime = 0; // Timestamp of last window roll
    uint8_t currentWindowIndex = 0;  // Current short-term window (0–3)

    // --- Sampling ---
    uint8_t samplingDenominator = SAMPLING_DENOMINATOR_INITIAL;
    uint8_t jitterOffset = 0xFF; // 0xFF = uninitialized
    bool jitterInitialized = false;

    // --- Adaptive Scaling ---
    uint8_t stepUpEvents = 0;
    uint8_t stepDownEvents = 0;
    uint8_t replacementCursor = 0;

    // --- Helper Methods ---

    /// Find or create entry for given node ID. Returns nullptr if at capacity and no existing entry.
    HistogramEntry *findOrCreateEntry(uint32_t nodeId);

    /// Find entry by node ID. Returns nullptr if not found.
    HistogramEntry *findEntry(uint32_t nodeId) const;

    /// Initialize jitter offset (called on first sampleRxPacket)
    void initializeJitter();

    /// Validate and clamp denominator to power-of-2 range
    static uint8_t validateDenominator(uint8_t d);

    /// Current clock source (millis in production, injectable in UNIT_TEST).
    static uint32_t nowMs();

#ifdef UNIT_TEST
    static bool s_useTestTime;
    static uint32_t s_testNowMs;
#endif
};

// --- Inline Bit Operation Functions ---

namespace CompactHistogramOps
{
/// Extract node ID from packed 16-bit entry (bits 0–12)
inline uint16_t getNodeId(uint16_t packed)
{
    return packed & 0x1FFF;
}

/// Extract hop count from packed 16-bit entry (bits 13–15)
inline uint8_t getHopCount(uint16_t packed)
{
    return (packed >> 13) & 0x7;
}

/// Pack node ID and hop count into 16-bit entry
inline uint16_t packNodeIdAndHops(uint32_t nodeId, uint8_t hops)
{
    return ((nodeId & 0x1FFF) | ((static_cast<uint16_t>(hops) & 0x7) << 13));
}

/// Mark node as seen in short-term window (windowIdx 0–3)
inline void markShortWindow(uint16_t &bitmap, uint8_t windowIdx)
{
    bitmap |= (1 << windowIdx);
}

/// Mark node as seen in long-term window (windowIdx 0–11)
inline void markLongWindow(uint16_t &bitmap, uint8_t windowIdx)
{
    bitmap |= (1 << (4 + windowIdx));
}

/// Check if node was seen in short-term window (bits 0–3)
inline bool wasSeenInShortTerm(uint16_t bitmap)
{
    return (bitmap & 0xF) != 0;
}

/// Check if node was seen in long-term window (bits 4–15)
inline bool wasSeenInLongTerm(uint16_t bitmap)
{
    return (bitmap & 0xFFF0) != 0;
}

/// Get short-term window bits (bits 0–3)
inline uint8_t getShortTermBits(uint16_t bitmap)
{
    return bitmap & 0xF;
}

/// Get long-term window bits (bits 4–15)
inline uint16_t getLongTermBits(uint16_t bitmap)
{
    return (bitmap >> 4) & 0xFFF;
}

/// Rotate short-term buffer left (discard oldest window)
inline uint16_t rotateShortTermBuffer(uint16_t bitmap)
{
    uint16_t shortBits = getShortTermBits(bitmap);
    shortBits = (shortBits << 1) & 0xF; // Shift left, discard overflow
    uint16_t longBits = getLongTermBits(bitmap);
    return (longBits << 4) | shortBits;
}
} // namespace CompactHistogramOps
