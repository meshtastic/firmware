#pragma once

#include <cstdint>

#include "mesh/MeshTypes.h"

enum class NotificationAudioCue : uint8_t {
    TX_TEXT = 0,
    RX_TEXT = 1,
};

static constexpr uint8_t NOTIFICATION_AUDIO_MIN_VOLUME = 0;
static constexpr uint8_t NOTIFICATION_AUDIO_MAX_VOLUME = 7;

class NotificationAudio
{
  public:
    virtual ~NotificationAudio() = default;

    virtual uint8_t getVolume() const = 0;
    virtual void setVolume(uint8_t volume) = 0;
    virtual bool queueCue(NotificationAudioCue cue) = 0;
    virtual void shutdown() = 0;
};

constexpr bool shouldPlayNotificationTxCue(uint32_t portnum, RxSource source, ErrorCode result)
{
    return portnum == meshtastic_PortNum_TEXT_MESSAGE_APP && source != RX_SRC_RADIO && result == ERRNO_OK;
}

constexpr bool shouldPlayNotificationRxCue(bool isRemote, bool isMuted, bool isSilenced, bool notificationsEnabled,
                                           bool directMessagesOnly, bool isDmToUs)
{
    return isRemote && !isMuted && !isSilenced && notificationsEnabled && (!directMessagesOnly || isDmToUs);
}
