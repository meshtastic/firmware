#pragma once

#include <stddef.h>
#include <stdint.h>

// A single tone in a system melody. Frequency is in Hz despite the historical
// field name; a frequency of NOTE_SILENT (1) or less is treated as a rest.
struct ToneDuration {
    int frequency_khz;
    int duration_ms;
};

/**
 * RTTTL parser and square-wave PCM generator.
 *
 * Replaces ESP8266Audio's AudioGeneratorRTTTL, which BackgroundAudio has no
 * equivalent for. The parse and synthesis math is ported from that generator
 * (GPL-3.0, Copyright (C) 2018 Earle F. Philhower, III) so existing user
 * ringtones keep their exact pitch and note lengths, including the double
 * integer truncation of note durations.
 *
 * This is a pull API: callers ask for N frames whenever the audio sink has room,
 * rather than the generator pushing into an output. It has no Arduino or ESP-IDF
 * dependency so it can be unit tested natively.
 */
class RtttlPcm
{
  public:
    /// Sample rate the generator emits at, matching AudioGeneratorRTTTL's default.
    static constexpr uint32_t kSampleRate = 22050;

    /// Peak amplitude. AudioGeneratorRTTTL emitted +/-8192 and AudioOutputI2S then
    /// applied SetGain(0.2) -> gainF2P6 = 12 -> 8192 * 12 >> 6 = 1536. ESP32I2SAudio
    /// has no gain stage, so the attenuation is baked in here instead.
    static constexpr int16_t kAmplitude = 1536;

    /// Longest melody buzz.cpp defines is 10 notes; round up for headroom.
    static constexpr size_t kMaxTones = 16;

    /// Parse an RTTTL song and arm the first note. Returns false if the header is
    /// malformed, in which case nothing is played.
    bool begin(const char *song, size_t len);

    /// Arm a system melody directly, skipping RTTTL entirely. The tones are copied,
    /// so callers may pass a stack array. At most kMaxTones are used.
    bool beginTones(const ToneDuration *tones, size_t count);

    /// Write up to maxFrames interleaved L/R frames. Returns the number of frames
    /// written, which is less than maxFrames only when the song has ended.
    size_t generate(int16_t *interleavedLR, size_t maxFrames);

    /// True once every note has been generated.
    bool done() const { return _done; }

    /// Abandon any song in progress.
    void reset();

  private:
    bool skipWhitespace();
    bool readInt(int *dest);
    bool parseHeader();
    bool nextNote();
    bool nextTone();
    bool advance();
    void startNote(int freqHz, int durationMs);

    // NUL-terminated copy of the song. meshtastic_RTTTLConfig.ringtone is 231
    // bytes. The upstream generator malloc()'d exactly len bytes with no
    // terminator and its digit loop had no bounds check, so any song ending in a
    // digit read past the allocation; a terminated fixed buffer removes both the
    // over-read and the allocation.
    char _song[256] = {0};
    int _len = 0;
    int _ptr = 0;

    // Direct tone-list playback, used instead of RTTTL for system melodies.
    ToneDuration _tones[kMaxTones] = {};
    size_t _toneCount = 0;
    size_t _toneIndex = 0;
    bool _toneMode = false;

    int _defaultDuration = 4;
    int _defaultOctave = 6;
    int _wholeNoteMs = 0;

    // Samples per wave period in 22.10 fixed point; 0 means the note is a rest.
    int32_t _samplesPerWaveFP10 = 0;
    // Phase accumulator, also 22.10. Replaces the upstream `samplesSent << 10`
    // expression, which overflowed int32 past 2^21 samples. Provably identical
    // because the period is always > 1024 for every note in the table.
    int32_t _phaseFP10 = 0;

    uint32_t _noteSamples = 0;
    uint32_t _samplesSent = 0;

    bool _done = true;
};
