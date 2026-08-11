#pragma once

#include <cstddef>
#include <cstdint>

#include "mesh/MeshTypes.h"

enum class A7682AudioCue : uint8_t {
    TX_TEXT = 0,
    RX_TEXT = 1,
};

static constexpr uint8_t A7682_AUDIO_MIN_VOLUME = 0;
static constexpr uint8_t A7682_AUDIO_MAX_VOLUME = 7;
static constexpr uint8_t A7682_AUDIO_DEFAULT_VOLUME = 4;

static constexpr uint32_t A7682_AUDIO_PREFERENCE_MAGIC = 0x41373632UL;
static constexpr uint8_t A7682_AUDIO_PREFERENCE_VERSION = 1;

static constexpr char A7682_AUDIO_TX_MP3_PATH[] = "C:/mesh_tx.mp3";
static constexpr char A7682_AUDIO_RX_MP3_PATH[] = "C:/mesh_rx.mp3";

struct A7682AudioPreferenceRecord {
    uint32_t magic;
    uint8_t version;
    uint8_t volume;
    uint16_t reserved;
};

static_assert(sizeof(A7682AudioPreferenceRecord) == 8, "A7682E audio preference layout changed");

constexpr uint8_t clampA7682AudioVolume(int volume)
{
    if (volume < A7682_AUDIO_MIN_VOLUME)
        return A7682_AUDIO_MIN_VOLUME;
    if (volume > A7682_AUDIO_MAX_VOLUME)
        return A7682_AUDIO_MAX_VOLUME;
    return static_cast<uint8_t>(volume);
}

constexpr bool shouldPlayA7682TxCue(uint32_t portnum, RxSource source, ErrorCode result)
{
    return portnum == meshtastic_PortNum_TEXT_MESSAGE_APP && source != RX_SRC_RADIO && result == ERRNO_OK;
}

constexpr bool shouldPlayA7682RxCue(bool isRemote, bool isMuted, bool isSilenced, bool notificationsEnabled,
                                    bool directMessagesOnly, bool isDmToUs)
{
    return isRemote && !isMuted && !isSilenced && notificationsEnabled && (!directMessagesOnly || isDmToUs);
}

constexpr const char *a7682AudioPathForCue(A7682AudioCue cue)
{
    return cue == A7682AudioCue::RX_TEXT ? A7682_AUDIO_RX_MP3_PATH : A7682_AUDIO_TX_MP3_PATH;
}

inline bool isValidA7682AudioPreferenceRecord(const A7682AudioPreferenceRecord &record, size_t bytesRead)
{
    return bytesRead == sizeof(record) && record.magic == A7682_AUDIO_PREFERENCE_MAGIC &&
           record.version == A7682_AUDIO_PREFERENCE_VERSION && record.volume <= A7682_AUDIO_MAX_VOLUME &&
           record.reserved == 0;
}
