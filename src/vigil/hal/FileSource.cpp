#ifndef ARDUINO // Desktop testing only

#include "FileSource.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace vigil {

/*
 * FileSource — WAV file reader for desktop testing.
 *
 * Reads standard RIFF/WAVE PCM files (16-bit, multi-channel).
 * Provides frame-by-frame AudioFrame output matching the
 * I2sTdmSource interface for platform-agnostic DSP testing.
 *
 *   WAV file ──► parse header ──► read samples ──► normalize to float
 *                                      │
 *                                      ▼
 *                          AudioFrame[NUM_CH][FRAME_SIZE]
 *                                      │
 *                                      ▼
 *                              DSP Pipeline (same code as ESP32)
 */

// WAV header structures (packed)
#pragma pack(push, 1)
struct RiffHeader {
    char riff[4];        // "RIFF"
    uint32_t file_size;
    char wave[4];        // "WAVE"
};

struct FmtChunk {
    char fmt[4];         // "fmt "
    uint32_t chunk_size;
    uint16_t audio_format;  // 1 = PCM
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
};

struct DataChunk {
    char data[4];        // "data"
    uint32_t data_size;
};
#pragma pack(pop)

// Internal state (PIMPL avoids exposing FILE* in header)
struct FileSourceState {
    FILE *file = nullptr;
    uint32_t data_offset = 0;
    uint32_t data_size = 0;
    uint16_t bits_per_sample = 0;
    uint16_t block_align = 0;
};

static FileSourceState g_state; // Single-instance for desktop testing

FileSource::FileSource(const std::string &wav_path)
    : path(wav_path)
{
}

FileSource::~FileSource()
{
    stop();
}

AudioError FileSource::init()
{
    if (is_initialized) return AudioError::OK;

    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return AudioError::NO_DATA;

    // Read RIFF header
    RiffHeader riff;
    if (fread(&riff, sizeof(riff), 1, f) != 1 ||
        memcmp(riff.riff, "RIFF", 4) != 0 ||
        memcmp(riff.wave, "WAVE", 4) != 0) {
        fclose(f);
        return AudioError::NO_DATA;
    }

    // Read fmt chunk
    FmtChunk fmt;
    if (fread(&fmt, sizeof(fmt), 1, f) != 1 ||
        memcmp(fmt.fmt, "fmt ", 4) != 0) {
        fclose(f);
        return AudioError::NO_DATA;
    }

    if (fmt.audio_format != 1) {
        // Only PCM supported
        fclose(f);
        return AudioError::NO_DATA;
    }

    // Skip any extra fmt bytes
    if (fmt.chunk_size > 16) {
        fseek(f, fmt.chunk_size - 16, SEEK_CUR);
    }

    // Find data chunk (skip non-data chunks)
    DataChunk dc;
    while (fread(&dc, sizeof(dc), 1, f) == 1) {
        if (memcmp(dc.data, "data", 4) == 0) break;
        fseek(f, dc.data_size, SEEK_CUR);
    }

    if (memcmp(dc.data, "data", 4) != 0) {
        fclose(f);
        return AudioError::NO_DATA;
    }

    num_channels = static_cast<uint8_t>(fmt.num_channels);
    sample_rate = fmt.sample_rate;
    g_state.file = f;
    g_state.data_offset = static_cast<uint32_t>(ftell(f));
    g_state.data_size = dc.data_size;
    g_state.bits_per_sample = fmt.bits_per_sample;
    g_state.block_align = fmt.block_align;

    uint32_t samples_per_channel = dc.data_size / fmt.block_align;
    total_frames = samples_per_channel / VIGIL_FRAME_SIZE;
    current_frame = 0;

    is_initialized = true;
    return AudioError::OK;
}

AudioError FileSource::getFrame(AudioFrame &frame)
{
    if (!is_initialized || !g_state.file) return AudioError::NOT_INITIALIZED;
    if (current_frame >= total_frames) return AudioError::NO_DATA;

    memset(&frame, 0, sizeof(frame));
    frame.num_channels = num_channels;
    frame.timestamp_us = static_cast<uint32_t>(
        (static_cast<uint64_t>(current_frame) * VIGIL_FRAME_SIZE * 1000000ULL) / sample_rate);

    // Set all channels healthy
    frame.mic_health = (num_channels >= 16) ? 0xFFFF : ((1u << num_channels) - 1);

    // Read interleaved samples
    const size_t samples_to_read = VIGIL_FRAME_SIZE * num_channels;
    const size_t bytes_per_sample = g_state.bits_per_sample / 8;

    if (g_state.bits_per_sample == 16) {
        // 16-bit PCM — most common
        auto *buf = static_cast<int16_t *>(malloc(samples_to_read * sizeof(int16_t)));
        if (!buf) return AudioError::NO_DATA;

        size_t read = fread(buf, sizeof(int16_t), samples_to_read, g_state.file);
        if (read < samples_to_read) {
            free(buf);
            return AudioError::NO_DATA;
        }

        // De-interleave and normalize to float [-1.0, 1.0]
        uint8_t ch_count = (num_channels > VIGIL_NUM_CHANNELS) ? VIGIL_NUM_CHANNELS : num_channels;
        for (size_t s = 0; s < VIGIL_FRAME_SIZE; s++) {
            for (uint8_t ch = 0; ch < ch_count; ch++) {
                frame.samples[ch][s] = buf[s * num_channels + ch] / 32768.0f;
            }
        }

        free(buf);
    } else if (g_state.bits_per_sample == 32) {
        // 32-bit PCM
        auto *buf = static_cast<int32_t *>(malloc(samples_to_read * sizeof(int32_t)));
        if (!buf) return AudioError::NO_DATA;

        size_t read = fread(buf, sizeof(int32_t), samples_to_read, g_state.file);
        if (read < samples_to_read) {
            free(buf);
            return AudioError::NO_DATA;
        }

        uint8_t ch_count = (num_channels > VIGIL_NUM_CHANNELS) ? VIGIL_NUM_CHANNELS : num_channels;
        for (size_t s = 0; s < VIGIL_FRAME_SIZE; s++) {
            for (uint8_t ch = 0; ch < ch_count; ch++) {
                frame.samples[ch][s] = buf[s * num_channels + ch] / 2147483648.0f;
            }
        }

        free(buf);
    } else {
        return AudioError::NO_DATA; // Unsupported bit depth
    }

    current_frame++;
    return AudioError::OK;
}

uint16_t FileSource::getMicHealth() const
{
    if (!is_initialized) return 0;
    return (num_channels >= 16) ? 0xFFFF : ((1u << num_channels) - 1);
}

uint8_t FileSource::getNumChannels() const
{
    return num_channels;
}

AudioError FileSource::start()
{
    if (!is_initialized) return AudioError::NOT_INITIALIZED;
    // Reset to beginning of data
    if (g_state.file) {
        fseek(g_state.file, g_state.data_offset, SEEK_SET);
        current_frame = 0;
    }
    return AudioError::OK;
}

AudioError FileSource::stop()
{
    if (g_state.file) {
        fclose(g_state.file);
        g_state.file = nullptr;
    }
    is_initialized = false;
    return AudioError::OK;
}

bool FileSource::isEof() const
{
    return current_frame >= total_frames;
}

} // namespace vigil

#endif // !ARDUINO
