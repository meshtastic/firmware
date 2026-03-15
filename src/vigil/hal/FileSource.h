#pragma once

#include "AudioSource.h"
#include <string>

#ifndef ARDUINO // Desktop testing only

namespace vigil {

/*
 * FileSource — Reads multi-channel WAV files for desktop testing.
 *
 * Used with the Python synthetic audio generator to create
 * repeatable test fixtures with known DoA angles.
 *
 * Architecture decision #7: HAL from day 1.
 * Architecture decision #19: Python synthetic audio generator.
 */
class FileSource : public AudioSource {
  public:
    explicit FileSource(const std::string &wav_path);
    ~FileSource() override;

    AudioError init() override;
    AudioError getFrame(AudioFrame &frame) override;
    uint16_t getMicHealth() const override;
    uint8_t getNumChannels() const override;
    AudioError start() override;
    AudioError stop() override;

    // Desktop-only: check if all frames have been consumed
    bool isEof() const;

  private:
    std::string path;
    bool is_initialized = false;
    uint8_t num_channels = 0;
    uint32_t sample_rate = 0;
    size_t current_frame = 0;
    size_t total_frames = 0;

    // TODO Phase 0: WAV file parsing, frame-by-frame reading
};

} // namespace vigil

#endif // !ARDUINO
