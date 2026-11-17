#pragma once

#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/generated/meshtastic/module_config.pb.h"
#include <stddef.h>
#include <stdint.h>

// Mode selection for codec2 voice. Airtime and channel utilisation arrive as
// arguments rather than from RadioLibInterface/AirTime, so this stays testable.

// libcodec2 mode ids, NOT the protobuf values. `bitrate - 1` happens to give the
// right id for every rate we ship except 450, where it yields an unused id and
// codec2_create() returns NULL. Always map through audioModeFromConfig().
enum AudioCodec2Mode : int8_t {
    AUDIO_C2_NONE = -1,
    AUDIO_C2_2400 = 1,
    AUDIO_C2_1400 = 3,
    AUDIO_C2_700C = 8,
    AUDIO_C2_450 = 10,
};

struct AudioModeInfo {
    meshtastic_ModuleConfig_AudioConfig_Audio_Baud config;
    int8_t codec2Mode;
    uint16_t bitrate; // for nearest-match fallback
    uint8_t bytesPerFrame;
    uint8_t frameMs;
};

// Best quality first - the degrade ladder walks forward, so order is load-bearing.
inline constexpr AudioModeInfo AUDIO_MODES[] = {
    {meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_2400, AUDIO_C2_2400, 2400, 6, 20},
    {meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_1400, AUDIO_C2_1400, 1400, 7, 40},
    {meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_700C, AUDIO_C2_700C, 700, 4, 40},
    {meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_450, AUDIO_C2_450, 450, 3, 40},
};
inline constexpr size_t AUDIO_MODE_COUNT = sizeof(AUDIO_MODES) / sizeof(AUDIO_MODES[0]);

// magic, mode, session, last, seq16 - see AudioModule.h
inline constexpr size_t AUDIO_HEADER_LEN = 6;
inline constexpr size_t AUDIO_PAYLOAD_LEN = meshtastic_Constants_DATA_PAYLOAD_LEN - AUDIO_HEADER_LEN;

// Share of the channel one talker occupies. 25% is what Meshtastic already
// treats as congested, and it leaves room for a second talker.
inline constexpr float AUDIO_OCCUPANCY_CEILING = 0.25f;
inline constexpr float AUDIO_CONGESTION_PERCENT = 25.0f;

inline uint16_t audioFramesPerPacket(const AudioModeInfo &m)
{
    return (uint16_t)(AUDIO_PAYLOAD_LEN / m.bytesPerFrame);
}

inline uint32_t audioMsPerPacket(const AudioModeInfo &m)
{
    return (uint32_t)audioFramesPerPacket(m) * m.frameMs;
}

// Packet airtime over speech that packet carries.
inline float audioOccupancy(const AudioModeInfo &m, uint32_t packetAirtimeMs)
{
    const uint32_t audioMs = audioMsPerPacket(m);
    return audioMs ? (float)packetAirtimeMs / (float)audioMs : 1e9f;
}

// Resolves DEFAULT and the removed 700/700B to 700C, and any rate this build
// does not carry to the nearest one it does. Null only for an out-of-enum value.
// Look a mode up by its libcodec2 number, for the receive side: the wire header
// carries the sender's mode, which need not be ours.
inline const AudioModeInfo *audioModeFromCodec2(int8_t codec2Mode)
{
    if (codec2Mode == AUDIO_C2_NONE)
        return nullptr;
    for (size_t i = 0; i < AUDIO_MODE_COUNT; i++)
        if (AUDIO_MODES[i].codec2Mode == codec2Mode)
            return &AUDIO_MODES[i];
    return nullptr;
}

inline const AudioModeInfo *audioModeFromConfig(meshtastic_ModuleConfig_AudioConfig_Audio_Baud cfg)
{
    switch (cfg) {
    case meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_DEFAULT:
    case meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_700:
    case meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_700B:
        cfg = meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_700C;
        break;
    default:
        break;
    }

    for (size_t i = 0; i < AUDIO_MODE_COUNT; i++)
        if (AUDIO_MODES[i].config == cfg)
            return &AUDIO_MODES[i];

    uint16_t wanted;
    switch (cfg) {
    case meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_3200:
        wanted = 3200;
        break;
    case meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_1600:
        wanted = 1600;
        break;
    case meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_1300:
        wanted = 1300;
        break;
    case meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_1200:
        wanted = 1200;
        break;
    default:
        return nullptr;
    }

    const AudioModeInfo *best = &AUDIO_MODES[0];
    int32_t bestDelta = INT32_MAX;
    for (size_t i = 0; i < AUDIO_MODE_COUNT; i++) {
        int32_t d = (int32_t)AUDIO_MODES[i].bitrate - (int32_t)wanted;
        if (d < 0)
            d = -d;
        if (d <= bestDelta) { // <= keeps the lower rate on a tie
            bestDelta = d;
            best = &AUDIO_MODES[i];
        }
    }
    return best;
}

// Latched once per PTT press. Degrades only, never upgrades. Returns null only
// when even the lowest rate cannot fit - the one case PTT should refuse.
inline const AudioModeInfo *chooseAudioMode(meshtastic_ModuleConfig_AudioConfig_Audio_Baud cfg, uint32_t packetAirtimeMs,
                                            float channelUtilPercent)
{
    const AudioModeInfo *m = audioModeFromConfig(cfg);
    if (!m)
        return nullptr;

    size_t i = (size_t)(m - AUDIO_MODES);

    while (audioOccupancy(AUDIO_MODES[i], packetAirtimeMs) > AUDIO_OCCUPANCY_CEILING && i + 1 < AUDIO_MODE_COUNT)
        i++;

    // Live congestion is a separate question from whether the mode fits the preset.
    if (channelUtilPercent > AUDIO_CONGESTION_PERCENT && i + 1 < AUDIO_MODE_COUNT)
        i++;

    if (audioOccupancy(AUDIO_MODES[i], packetAirtimeMs) > AUDIO_OCCUPANCY_CEILING)
        return nullptr;

    return &AUDIO_MODES[i];
}
