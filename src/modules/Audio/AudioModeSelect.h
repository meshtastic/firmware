#pragma once

#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/generated/meshtastic/module_config.pb.h"
#include <stddef.h>
#include <stdint.h>

/**
 * Codec2 mode selection for the audio module.
 *
 * Deliberately free of radio, hardware and Arduino dependencies: packet airtime
 * and channel utilisation arrive as arguments rather than being read from
 * RadioLibInterface and AirTime. That keeps the part which decides whether PTT
 * works at all testable on the host, where the arithmetic can be pinned down.
 */

/**
 * Upstream libcodec2 mode ids.
 *
 * These are NOT the protobuf enum values, and the difference is a trap. Codec2
 * removed modes 700 and 700B and renumbered their replacements, so the old
 * `bitrate - 1` mapping now yields the right id for every mode we ship except
 * 450 - which silently becomes 9, an id no mode uses, so codec2_create()
 * returns NULL. A partial fix therefore passes every test run at the default
 * rate and fails only on the mode the emergency case depends on. Map through
 * audioModeFromConfig() and never re-derive this arithmetically.
 */
enum AudioCodec2Mode : int8_t {
    AUDIO_C2_NONE = -1,
    AUDIO_C2_2400 = 1,
    AUDIO_C2_1400 = 3,
    AUDIO_C2_700C = 8,
    AUDIO_C2_450 = 10,
};

struct AudioModeInfo {
    meshtastic_ModuleConfig_AudioConfig_Audio_Baud config; // wire/config value
    int8_t codec2Mode;                                     // libcodec2 mode id
    uint16_t bitrate;                                      // bit/s, for nearest-match fallback
    uint8_t bytesPerFrame;
    uint8_t frameMs;
};

/**
 * The modes this firmware builds, ordered best quality first. The degrade
 * ladder walks forward through this array, so the order is load-bearing.
 *
 * 3200 is excluded on airtime: 300 bytes per second of speech buys nothing over
 * 2400 on any link. 1600 is excluded because 1400 uses the identical 36-bit LSP
 * quantiser and reaches the same spectral envelope in eight fewer bits. 1300 is
 * dominated by 1400 outright - both pack to 7 bytes, so 1300 costs the same
 * airtime and sounds worse. 1200 wants a 43 KB codebook to gain 1.17x over
 * 1400. 450PWB is a decoder rendering option, not a bitrate.
 */
inline constexpr AudioModeInfo AUDIO_MODES[] = {
    {meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_2400, AUDIO_C2_2400, 2400, 6, 20},
    {meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_1400, AUDIO_C2_1400, 1400, 7, 40},
    {meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_700C, AUDIO_C2_700C, 700, 4, 40},
    {meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_450, AUDIO_C2_450, 450, 3, 40},
};
inline constexpr size_t AUDIO_MODE_COUNT = sizeof(AUDIO_MODES) / sizeof(AUDIO_MODES[0]);

/// Wire header: magic, mode, session, last, seq16. See AudioModule.h.
inline constexpr size_t AUDIO_HEADER_LEN = 6;
inline constexpr size_t AUDIO_PAYLOAD_LEN = meshtastic_Constants_DATA_PAYLOAD_LEN - AUDIO_HEADER_LEN;

/**
 * Fraction of the channel one talker holding PTT occupies: airtime a packet
 * costs over speech that packet carries. Above 1.0 the node cannot even keep
 * up with itself.
 */
inline constexpr float AUDIO_OCCUPANCY_CEILING = 0.25f;

/// Channel utilisation above which we step down an extra rung, in percent.
inline constexpr float AUDIO_CONGESTION_PERCENT = 25.0f;

inline uint16_t audioFramesPerPacket(const AudioModeInfo &m)
{
    return (uint16_t)(AUDIO_PAYLOAD_LEN / m.bytesPerFrame);
}

inline uint32_t audioMsPerPacket(const AudioModeInfo &m)
{
    return (uint32_t)audioFramesPerPacket(m) * m.frameMs;
}

inline float audioOccupancy(const AudioModeInfo &m, uint32_t packetAirtimeMs)
{
    const uint32_t audioMs = audioMsPerPacket(m);
    if (audioMs == 0)
        return 1e9f; // treat as impossible rather than dividing by zero
    return (float)packetAirtimeMs / (float)audioMs;
}

/**
 * Map a configured bitrate to a built mode.
 *
 * Handles three cases the config can present. `CODEC2_DEFAULT` and the removed
 * `CODEC2_700` / `CODEC2_700B` resolve to 700C. A rate this build does not carry
 * (3200, 1600, 1300, 1200) resolves to the nearest by bitrate, preferring the
 * lower on a tie - the occupancy gate then steps it down further if the preset
 * cannot afford it. Returns nullptr only for a value outside the enum.
 */
inline const AudioModeInfo *audioModeFromConfig(meshtastic_ModuleConfig_AudioConfig_Audio_Baud cfg)
{
    switch (cfg) {
    case meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_DEFAULT:
    case meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_700:  // removed upstream
    case meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_700B: // removed upstream
        cfg = meshtastic_ModuleConfig_AudioConfig_Audio_Baud_CODEC2_700C;
        break;
    default:
        break;
    }

    for (size_t i = 0; i < AUDIO_MODE_COUNT; i++)
        if (AUDIO_MODES[i].config == cfg)
            return &AUDIO_MODES[i];

    // Not built. Fall back to the nearest rate we do carry.
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
        return nullptr; // outside the enum entirely
    }

    const AudioModeInfo *best = &AUDIO_MODES[0];
    int32_t bestDelta = INT32_MAX;
    for (size_t i = 0; i < AUDIO_MODE_COUNT; i++) {
        int32_t d = (int32_t)AUDIO_MODES[i].bitrate - (int32_t)wanted;
        if (d < 0)
            d = -d;
        // AUDIO_MODES runs high to low, so <= keeps the lower rate on a tie.
        if (d <= bestDelta) {
            bestDelta = d;
            best = &AUDIO_MODES[i];
        }
    }
    return best;
}

/**
 * Pick the mode to transmit at, latched once per PTT press.
 *
 * Two independent steps. The occupancy walk answers "is this mode too fat for
 * this preset", which is static and only changes with config. The congestion
 * step answers "is the channel already busy", which is live. They are different
 * questions, so one does not subsume the other.
 *
 * Never returns a mode above the configured one - degradation only.
 *
 * @param packetAirtimeMs airtime of a full DATA_PAYLOAD_LEN packet on the
 *                        current radio settings, from getPacketTime()
 * @param channelUtilPercent airTime->channelUtilizationPercent()
 * @return the chosen mode, or nullptr when even the lowest rate cannot fit -
 *         the only case where PTT should refuse
 */
inline const AudioModeInfo *chooseAudioMode(meshtastic_ModuleConfig_AudioConfig_Audio_Baud cfg, uint32_t packetAirtimeMs,
                                            float channelUtilPercent)
{
    const AudioModeInfo *m = audioModeFromConfig(cfg);
    if (!m)
        return nullptr;

    size_t i = (size_t)(m - AUDIO_MODES);

    // Too fat for this preset - step down until it fits.
    while (audioOccupancy(AUDIO_MODES[i], packetAirtimeMs) > AUDIO_OCCUPANCY_CEILING && i + 1 < AUDIO_MODE_COUNT)
        i++;

    // Channel already busy - give up one more rung if there is one. Worse audio
    // beats no audio, and beats making the congestion worse.
    if (channelUtilPercent > AUDIO_CONGESTION_PERCENT && i + 1 < AUDIO_MODE_COUNT)
        i++;

    if (audioOccupancy(AUDIO_MODES[i], packetAirtimeMs) > AUDIO_OCCUPANCY_CEILING)
        return nullptr; // nothing fits, refuse

    return &AUDIO_MODES[i];
}
