#pragma once

// nRF52840 I2S tone driver. Drives NRF_I2S registers directly via nrf_i2s.h
// (the framework has no nrfx_i2s.c driver, only the register HAL).
// Plays repeating square-wave tones only - no RTTTL text parsing.

#include <Arduino.h>

class NRF52I2SOutput
{
  public:
    bool begin(uint8_t sckPin, uint8_t wsPin, uint8_t sdPin);

    // frequencyHz <= 1 = silent rest.
    void playTone(uint32_t frequencyHz, uint32_t durationMs);

    // Non-blocking variants for callers that advance timing themselves (e.g. RTTTL player).
    void startTone(uint32_t frequencyHz);
    void stopTone();

    void end();

  private:
    void fillBuffer(uint32_t frequencyHz);

    static constexpr uint32_t kSampleRateHz = 15625; // 32MHz/8/256
    static constexpr size_t kMaxSamples = 512;

    uint32_t buffer[kMaxSamples] = {};
    bool started = false;
};

extern NRF52I2SOutput nrf52I2SOutput;
