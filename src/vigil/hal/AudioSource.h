#pragma once

#include <cstddef>
#include <cstdint>

namespace vigil {

/*
 * AudioSource — Hardware Abstraction Layer for multi-channel audio input.
 *
 * Implementations:
 *   I2sTdmSource  — ESP32-S3 I2S TDM driver (16x ICS-52000 MEMS)
 *   FileSource    — Desktop testing (reads WAV files from disk)
 *
 * Architecture decision #7: HAL from day 1 — DSP/triangulation
 * testable on desktop without hardware.
 *
 *   ┌──────────────┐     ┌──────────────┐
 *   │ I2sTdmSource │     │  FileSource  │
 *   │  (ESP32-S3)  │     │  (desktop)   │
 *   └──────┬───────┘     └──────┬───────┘
 *          │                    │
 *          ▼                    ▼
 *   ┌──────────────────────────────────┐
 *   │     AudioSource interface        │
 *   │  getFrame() → float[NUM_CH][N]   │
 *   └──────────────┬───────────────────┘
 *                  │
 *                  ▼
 *          DSP Pipeline (platform-agnostic)
 */

// Configurable at compile time (config.h)
#ifndef VIGIL_NUM_CHANNELS
#define VIGIL_NUM_CHANNELS 16
#endif

#ifndef VIGIL_FRAME_SIZE
#define VIGIL_FRAME_SIZE 1024
#endif

// Result type for operations that can fail (architecture decision #17)
enum class AudioError : uint8_t {
    OK = 0,
    NOT_INITIALIZED,
    DMA_OVERRUN,
    MIC_FAULT,
    NO_DATA,
    TIMEOUT,
};

struct AudioFrame {
    float samples[VIGIL_NUM_CHANNELS][VIGIL_FRAME_SIZE];
    uint32_t timestamp_us;   // Microsecond timestamp (from GPS PPS or system clock)
    uint16_t mic_health;     // Bitmask: bit N = 1 means channel N is healthy
    uint8_t num_channels;    // Actual number of active channels
};

class AudioSource {
  public:
    virtual ~AudioSource() = default;

    // Initialize the audio source. Returns error if hardware setup fails.
    virtual AudioError init() = 0;

    // Capture one frame of audio. Blocks until data is ready or timeout.
    // On success, fills `frame` and returns OK.
    virtual AudioError getFrame(AudioFrame &frame) = 0;

    // Return mic health bitmask (bit N = channel N healthy)
    virtual uint16_t getMicHealth() const = 0;

    // Number of active channels
    virtual uint8_t getNumChannels() const = 0;

    // Start/stop audio capture (for power management)
    virtual AudioError start() = 0;
    virtual AudioError stop() = 0;
};

} // namespace vigil
