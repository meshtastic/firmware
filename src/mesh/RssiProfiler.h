#pragma once

#include <stdint.h>

// Rolling distribution of a signed dB delta - e.g. (instantaneous RSSI - rolling noise floor). Used by
// the RSSI-margin profiler behind LORA_RSSI_LBT_PROFILE to pick RSSI_LBT_MARGIN_DB from measured data.
// Deliberately dependency-free and radio-agnostic: it only accumulates and reports statistics. The RSSI
// reads, the noise-floor reference and the logging all live in RadioLibInterface. See
// .notes/lbt-rssi-margin-profiler.md.
struct RssiDeltaStats {
    static constexpr int16_t BIN_WIDTH_DB = 2;
    // bin[0] = underflow (delta < 0 dB); bins 1..NBINS-2 cover 0..(BIN_WIDTH_DB*(NBINS-2)) dB above the
    // floor; the last bin is the overflow catch-all.
    static constexpr uint8_t NBINS = 34; // covers up to ~64 dB above floor

    uint32_t count = 0;
    int16_t minDelta = 0;
    int16_t maxDelta = 0;
    int32_t sumDelta = 0;
    uint32_t hist[NBINS] = {0};

    void add(int16_t deltaDb)
    {
        if (count == 0) {
            minDelta = maxDelta = deltaDb;
        } else if (deltaDb < minDelta) {
            minDelta = deltaDb;
        } else if (deltaDb > maxDelta) {
            maxDelta = deltaDb;
        }
        count++;
        sumDelta += deltaDb;

        int idx = (deltaDb < 0) ? 0 : (deltaDb / BIN_WIDTH_DB) + 1;
        if (idx >= NBINS)
            idx = NBINS - 1; // clamp into the overflow bin
        hist[idx]++;
    }

    int16_t mean() const { return count ? (int16_t)(sumDelta / (int32_t)count) : 0; }

    // Approx dB delta below which `pct`% of samples fall (bin-edge resolution).
    int16_t percentile(uint8_t pct) const
    {
        if (count == 0)
            return 0;
        uint32_t target = (uint64_t)count * pct / 100;
        uint32_t acc = 0;
        for (uint8_t i = 0; i < NBINS; i++) {
            acc += hist[i];
            if (acc >= target)
                return (i == 0) ? minDelta : (int16_t)((i - 1) * BIN_WIDTH_DB);
        }
        return maxDelta;
    }

    void reset()
    {
        count = 0;
        minDelta = 0;
        maxDelta = 0;
        sumDelta = 0;
        for (uint8_t i = 0; i < NBINS; i++)
            hist[i] = 0;
    }
};
