#pragma once

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
    meshtastic_Config_LoRaConfig previous;
    meshtastic_Config_LoRaConfig candidate;
    uint32_t requestedAtMsec;
    uint32_t timeoutMsec;
    std::atomic<RadioConfigApplyResult> result{RadioConfigApplyResult::IDLE};
    bool previousLicensed = false;
    bool candidateLicensed = false;
    uint32_t acceptedRadioId = 0;
};
