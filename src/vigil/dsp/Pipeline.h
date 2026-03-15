#pragma once

#include "../hal/AudioSource.h"
#include <cstdint>

namespace vigil {

/*
 * DSP Pipeline — Audio processing chain for drone detection.
 *
 * Architecture decision #13: Adaptive SRP-PHAT grid (10°→2°).
 *
 *   AudioFrame ──► HPF (>150Hz) ──► FFT (512-pt) ──► SRP-PHAT
 *                                                       │
 *                                    ┌──────────────────┘
 *                                    ▼
 *                            Coarse scan (10°)
 *                            324 grid points
 *                                    │
 *                              peaks found?
 *                              ╱          ╲
 *                           no              yes
 *                           │                │
 *                        silence         Fine scan (2°)
 *                                       ~100 pts/peak
 *                                            │
 *                                            ▼
 *                                    DoA vectors + cluster IDs
 */

struct DoAResult {
    float azimuth_deg;     // 0-360° relative to array (before heading offset)
    float elevation_deg;   // 0-90° above horizontal
    float confidence;      // 0.0-1.0 (SRP-PHAT power normalized)
    uint8_t cluster_id;    // Assigned by peak extractor
};

enum class PipelineError : uint8_t {
    OK = 0,
    NO_DETECTION,
    SIGNAL_CLIPPING,
    COMPUTE_OVERRUN,
    MATH_ERROR,
};

struct PipelineResult {
    PipelineError error;
    DoAResult detections[4]; // Max 4 simultaneous targets
    uint8_t num_detections;
    float noise_floor_dba;   // Ambient noise level
    uint32_t compute_time_us;
};

// Mic geometry: 3D positions of each microphone relative to array center (meters)
struct MicPosition {
    float x, y, z;
};

class Pipeline {
  public:
    // Initialize with mic geometry (defines beamforming steering vectors)
    void init(const MicPosition *mic_positions, uint8_t num_mics, uint32_t sample_rate);

    // Process one audio frame → detection results
    PipelineResult process(const AudioFrame &frame);

  private:
    const MicPosition *mic_pos = nullptr;
    uint8_t num_mics = 0;
    uint32_t sample_rate = 0;
    bool is_initialized = false;

    // TODO Phase A: HPF state, FFT working buffers, SRP-PHAT grid
    // TODO Phase A: Adaptive grid (coarse 10° then fine 2° around peaks)
    // TODO Phase B: Multi-peak extraction + cluster ID assignment
};

} // namespace vigil
