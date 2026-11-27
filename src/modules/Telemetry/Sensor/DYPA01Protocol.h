#pragma once

#include <stdint.h>

#ifndef DYP_A01_BURST_SAMPLES
#define DYP_A01_BURST_SAMPLES 9
#endif

#ifndef DYP_A01_MIN_VALID_SAMPLES
#define DYP_A01_MIN_VALID_SAMPLES 5
#endif

// Too few checksum-valid samples means the sensor is quiet or disconnected, so the
// burst must not be published (a stale water level would otherwise stay on the mesh).
inline bool dypA01BurstPublishable(uint8_t validSamples)
{
    return validSamples >= DYP_A01_MIN_VALID_SAMPLES;
}

inline uint8_t dypA01Checksum(uint8_t dataH, uint8_t dataL)
{
    return (uint8_t)((0xFF + dataH + dataL) & 0xFF);
}

// Upper median of a real sample. Even counts are not averaged: a short false echo
// must not invent a distance the sensor never measured.
inline float dypA01MedianMm(float *samples, uint8_t count)
{
    for (uint8_t i = 1; i < count; i++) {
        float key = samples[i];
        int j = (int)i - 1;
        while (j >= 0 && samples[j] > key) {
            samples[j + 1] = samples[j];
            j--;
        }
        samples[j + 1] = key;
    }
    return samples[count / 2];
}

enum class DypA01Parse : uint8_t { NeedMore = 0, Accepted = 1, Rejected = 2 };

// DYP-A01 frame: 0xFF, Data_H, Data_L, SUM=(0xFF+Data_H+Data_L)&0xFF. Distance is millimetres.
class DypA01Parser
{
  public:
    void reset() { len = 0; }

    DypA01Parse push(uint8_t byte, float &distanceMm)
    {
        if (len == 0) {
            if (byte != 0xFF)
                return DypA01Parse::NeedMore;
            frame[0] = byte;
            len = 1;
            return DypA01Parse::NeedMore;
        }

        frame[len++] = byte;
        if (len < 4)
            return DypA01Parse::NeedMore;

        const uint8_t dataH = frame[1];
        const uint8_t dataL = frame[2];
        const uint8_t sum = frame[3];
        len = 0;

        if (sum == dypA01Checksum(dataH, dataL)) {
            distanceMm = (float)(((uint16_t)dataH << 8) | dataL);
            return DypA01Parse::Accepted;
        }

        // 0xFF is legal payload, so a rejected frame can contain the next header.
        const uint8_t replay[3] = {dataH, dataL, sum};
        for (uint8_t b : replay) {
            if (len == 0) {
                if (b == 0xFF) {
                    frame[0] = b;
                    len = 1;
                }
            } else {
                frame[len++] = b;
            }
        }
        if (len > 0)
            return DypA01Parse::NeedMore;
        return DypA01Parse::Rejected;
    }

  private:
    uint8_t frame[4] = {0, 0, 0, 0};
    uint8_t len = 0;
};
