#pragma once

#include "AudioSource.h"

#ifdef ARDUINO // Only compile on ESP32

namespace vigil {

/*
 * I2sTdmSource — ESP32-S3 I2S TDM driver for ICS-52000 MEMS array.
 *
 * Architecture decision #18: 1024-sample DMA double buffer.
 * Architecture decision #2 (eng): DSP pinned to Core 0.
 *
 * Hardware: 16x TDK ICS-52000 MEMS microphones on TDM bus.
 *   - I2S0: 16 channels via TDM (4 chips x 4 slots per data line)
 *   - DMA double-buffering: 2 x 1024 samples x 16 channels x 16-bit
 *   - Total DMA buffer: ~64KB
 *
 *   ESP32-S3 I2S0 ──► DMA ──► Ring Buffer ──► AudioFrame
 *        │
 *        └── ISR on Core 0 (pinned)
 */
class I2sTdmSource : public AudioSource {
  public:
    I2sTdmSource() = default;
    ~I2sTdmSource() override;

    AudioError init() override;
    AudioError getFrame(AudioFrame &frame) override;
    uint16_t getMicHealth() const override;
    uint8_t getNumChannels() const override;
    AudioError start() override;
    AudioError stop() override;

  private:
    bool is_initialized = false;
    uint16_t mic_health_mask = 0;

    // TODO Phase A: Add I2S handle, DMA buffer pointers, ISR registration
};

} // namespace vigil

#endif // ARDUINO
