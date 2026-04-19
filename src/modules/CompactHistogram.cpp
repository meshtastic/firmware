#include "CompactHistogram.h"
#include <Arduino.h>
#include <cmath>
#include <cstdlib>

#ifdef UNIT_TEST
bool CompactHistogram::s_useTestTime = false;
uint32_t CompactHistogram::s_testNowMs = 0;
#endif

uint32_t CompactHistogram::nowMs()
{
#ifdef UNIT_TEST
    if (s_useTestTime) {
        return s_testNowMs;
    }
#endif
    return millis();
}

#ifdef UNIT_TEST
void CompactHistogram::setTimeForTest(uint32_t nowMsValue)
{
    s_testNowMs = nowMsValue;
    s_useTestTime = true;
}

void CompactHistogram::clearTimeForTest()
{
    s_useTestTime = false;
}
#endif

CompactHistogram::CompactHistogram() : sessionStartTime(nowMs()), lastWindowRollTime(nowMs()), currentWindowIndex(0)
{
    clear();
}

void CompactHistogram::clear()
{
    memset(entries, 0, sizeof(entries));
    count = 0;
    sessionStartTime = nowMs();
    lastWindowRollTime = nowMs();
    currentWindowIndex = 0;

    // On explicit clear, re-randomize jitter offset
    jitterOffset = rand() % samplingDenominator;
    jitterInitialized = true;
}

void CompactHistogram::initializeJitter()
{
    if (!jitterInitialized) {
        jitterOffset = rand() % samplingDenominator;
        jitterInitialized = true;
    }
}

void CompactHistogram::sampleRxPacket(uint32_t nodeId, uint8_t hopCount)
{
    // Initialize jitter on first call
    if (!jitterInitialized) {
        initializeJitter();
    }

    // Bitwise modulo sampling: only process if (nodeId & (denominator - 1)) == jitterOffset
    if ((nodeId & (samplingDenominator - 1)) != jitterOffset) {
        return;
    }

    // Update current window index
    updateCurrentWindowIndex();

    // Clamp hop count to 3-bit range
    hopCount = std::min(hopCount, static_cast<uint8_t>(MAX_HOP));

    // Find or create entry for this node
    HistogramEntry *entry = findOrCreateEntry(nodeId);
    if (!entry) {
        return; // Capacity exceeded, no room for new entry
    }

    // Extract current hop count from entry
    uint8_t currentHop = CompactHistogramOps::getHopCount(entry->nodeIdAndHops);

    // Store minimum observed hop count
    if (hopCount < currentHop || currentHop == 0) {
        entry->nodeIdAndHops = CompactHistogramOps::packNodeIdAndHops(nodeId, hopCount);
    }

    // Mark in current short-term window
    CompactHistogramOps::markShortWindow(entry->windowBitmap, currentWindowIndex);
}

HistogramEntry *CompactHistogram::findOrCreateEntry(uint32_t nodeId)
{
    // Search for existing entry
    auto *entry = findEntry(nodeId);
    if (entry) {
        return entry;
    }

    // Create new entry if there's room
    if (count < CAPACITY) {
        entry = &entries[count++];
        entry->nodeIdAndHops = CompactHistogramOps::packNodeIdAndHops(nodeId, 0);
        entry->windowBitmap = 0;
        return entry;
    }

    // Capacity exceeded: drop oldest entry (index 0)
    // This is a simple LRU-like policy: new entries push out old ones
    entry = &entries[0];
    entry->nodeIdAndHops = CompactHistogramOps::packNodeIdAndHops(nodeId, 0);
    entry->windowBitmap = 0;
    return entry;
}

HistogramEntry *CompactHistogram::findEntry(uint32_t nodeId) const
{
    for (size_t i = 0; i < count; i++) {
        if (CompactHistogramOps::getNodeId(entries[i].nodeIdAndHops) == nodeId) {
            return const_cast<HistogramEntry *>(&entries[i]);
        }
    }
    return nullptr;
}

void CompactHistogram::updateCurrentWindowIndex()
{
    uint32_t now = nowMs();
    uint32_t elapsed = now - sessionStartTime;
    currentWindowIndex = (elapsed / SHORT_WINDOW_DURATION_MS) % SHORT_TERM_WINDOWS;
}

bool CompactHistogram::isWindowRollDue() const
{
    uint32_t now = nowMs();
    return (now - lastWindowRollTime) >= SHORT_WINDOW_DURATION_MS;
}

void CompactHistogram::rollWindows()
{
    uint32_t now = nowMs();

    // Check if 30 minutes have passed
    if ((now - lastWindowRollTime) < SHORT_WINDOW_DURATION_MS) {
        return;
    }

    lastWindowRollTime = now;

    // Determine which short-term window is expiring (oldest one)
    uint8_t expiringShortIdx = (currentWindowIndex + 1) % SHORT_TERM_WINDOWS;
    uint16_t expiringBits = (1 << expiringShortIdx);

    // Aggregate expiring short window into appropriate long-term window
    uint32_t elapsed = now - sessionStartTime;
    uint32_t longWindowCount = elapsed / LONG_WINDOW_DURATION_MS;
    uint8_t targetLongIdx = longWindowCount % LONG_TERM_WINDOWS;

    for (size_t i = 0; i < count; i++) {
        if (entries[i].windowBitmap & expiringBits) {
            // This node was seen in the expiring short window
            // Promote it to the long-term buffer
            CompactHistogramOps::markLongWindow(entries[i].windowBitmap, targetLongIdx);
        }
    }

    // Rotate short-term buffer: shift bits left, zero out oldest
    for (size_t i = 0; i < count; i++) {
        entries[i].windowBitmap = CompactHistogramOps::rotateShortTermBuffer(entries[i].windowBitmap);
    }

    // Update current window index
    updateCurrentWindowIndex();
}

CompactHistogram::HopDistribution CompactHistogram::getHopDistribution(bool recentOnly) const
{
    std::vector<uint8_t> hops;

    for (size_t i = 0; i < count; i++) {
        // Filter by recency if requested (only nodes seen in last 2 hours)
        if (recentOnly) {
            if (!CompactHistogramOps::wasSeenInShortTerm(entries[i].windowBitmap)) {
                continue;
            }
        }

        uint8_t hopCount = CompactHistogramOps::getHopCount(entries[i].nodeIdAndHops);
        hops.push_back(hopCount);
    }

    HopDistribution dist;

    if (hops.empty()) {
        return dist; // All zeros
    }

    std::sort(hops.begin(), hops.end());

    dist.minHops = hops.front();
    dist.maxHops = hops.back();
    dist.sampleCount = hops.size();
    dist.medianHops = hops[hops.size() / 2];
    dist.percentile25Hops = hops[(hops.size() * 1) / 4];
    dist.percentile75Hops = hops[(hops.size() * 3) / 4];

    return dist;
}

CompactHistogram::PerHopDistribution CompactHistogram::getPerHopDistribution(bool recentOnly) const
{
    PerHopDistribution dist;
    memset(dist.perHop, 0, sizeof(dist.perHop));
    dist.totalSamples = 0;

    for (size_t i = 0; i < count; i++) {
        // Filter by recency if requested (only nodes seen in last 2 hours)
        if (recentOnly && !CompactHistogramOps::wasSeenInShortTerm(entries[i].windowBitmap)) {
            continue;
        }

        const uint8_t hopCount = CompactHistogramOps::getHopCount(entries[i].nodeIdAndHops);
        if (hopCount <= MAX_HOP) {
            dist.perHop[hopCount]++;
            dist.totalSamples++;
        }
    }

    return dist;
}

void CompactHistogram::setSamplingDenominator(uint8_t denominator)
{
    samplingDenominator = validateDenominator(denominator);

    // If jitter was already initialized, adjust it to the new denominator
    if (jitterInitialized) {
        jitterOffset = jitterOffset % samplingDenominator;
    }
}

uint8_t CompactHistogram::validateDenominator(uint8_t d)
{
    // Ensure it's a power of 2
    if (d == 0) {
        return SAMPLING_DENOMINATOR_INITIAL;
    }

    // Check if power of 2: (d & (d - 1)) == 0
    if ((d & (d - 1)) != 0) {
        // Not a power of 2, find nearest smaller power of 2
        uint8_t result = 1;
        while (result < d) {
            const uint16_t next = static_cast<uint16_t>(result) << 1;
            if (next > d || next > 0xFF)
                break;
            result = static_cast<uint8_t>(next);
        }
        d = result;
    }

    // Clamp to valid range
    if (d < SAMPLING_DENOMINATOR_MIN) {
        d = SAMPLING_DENOMINATOR_MIN;
    }
    if (d > SAMPLING_DENOMINATOR_MAX) {
        d = SAMPLING_DENOMINATOR_MAX;
    }

    return d;
}

bool CompactHistogram::shouldScaleUpDenominator() const
{
    // Scale up if histogram is >90% full
    return getFillPercentage() > 90;
}

bool CompactHistogram::shouldScaleDownDenominator() const
{
    // Scale down if histogram is <20% full
    return getFillPercentage() < 20;
}
