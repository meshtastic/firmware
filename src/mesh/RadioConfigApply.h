#pragma once

#include "meshtastic/channel.pb.h"
#include "meshtastic/config.pb.h"
#include <atomic>
#include <stdint.h>

enum class RadioConfigApplyResult : uint8_t {
    IDLE,
    PENDING,
    APPLIED,
    TIMED_OUT,
    APPLY_FAILED_ROLLED_BACK,
    ROLLBACK_FAILED,
    INTERFACE_REPLACED,
    BUSY,
};

struct RadioConfigApplyRequest {
    meshtastic_Config_LoRaConfig previous = meshtastic_Config_LoRaConfig_init_zero;
    meshtastic_Config_LoRaConfig candidate = meshtastic_Config_LoRaConfig_init_zero;
    uint32_t requestedAtMsec = 0;
    uint32_t timeoutMsec = 0;
    std::atomic<RadioConfigApplyResult> result{RadioConfigApplyResult::IDLE};
    bool previousLicensed = false;
    bool candidateLicensed = false;
    uint32_t acceptedRadioId = 0;
    meshtastic_ChannelSettings previousPrimary = meshtastic_ChannelSettings_init_zero;
    meshtastic_ChannelSettings candidatePrimary = meshtastic_ChannelSettings_init_zero;
    bool hasPrimarySnapshots = false;
};
