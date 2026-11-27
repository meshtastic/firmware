/*
 * Pure helpers for Heltec Vision Master E290 fast-refresh waveform selection.
 * No display hardware dependency - safe for native unit tests.
 */

#pragma once

#include <cstdint>

namespace NicheGraphics::Drivers
{

// Matches EInk::UpdateTypes bit values (FULL=1, FAST=2) without pulling in the driver.
enum HeltecVME290UpdateType : uint8_t {
    HELTEC_VME290_FULL = 1 << 0,
    HELTEC_VME290_FAST = 1 << 1,
};

enum class HeltecVME290Waveform : uint8_t {
    Unset = 0,
    CustomLut = 1, // flex 15-22243-M0282
    OtpFast = 2,   // flex FPC-7519revb
};

// Until a panel choice is saved, FAST must not run - promote to FULL (works on both flexes).
constexpr bool heltecVME290PromoteFastToFull(HeltecVME290Waveform mode)
{
    return mode == HeltecVME290Waveform::Unset;
}

constexpr uint8_t heltecVME290EffectiveType(HeltecVME290Waveform mode, uint8_t updateType)
{
    if (updateType == HELTEC_VME290_FAST && heltecVME290PromoteFastToFull(mode))
        return HELTEC_VME290_FULL;
    return updateType;
}

// SSD16xx register 0x22 data byte after effective type is applied.
constexpr uint8_t heltecVME290UpdateSequence(HeltecVME290Waveform mode, uint8_t updateType)
{
    const uint8_t effective = heltecVME290EffectiveType(mode, updateType);
    if (effective == HELTEC_VME290_FAST) {
        if (mode == HeltecVME290Waveform::CustomLut)
            return 0xCF; // differential, MCU-loaded LUT
        return 0xFF;     // OTP mode 2 (OtpFast; Unset never reaches FAST)
    }
    return 0xF7; // OTP full refresh
}

// Short labels for on-screen menu / confirmation banner.
inline const char *heltecVME290WaveformLabel(HeltecVME290Waveform mode)
{
    switch (mode) {
    case HeltecVME290Waveform::CustomLut:
        return "Custom refresh";
    case HeltecVME290Waveform::OtpFast:
        return "OTP refresh";
    case HeltecVME290Waveform::Unset:
    default:
        return "Not set";
    }
}

} // namespace NicheGraphics::Drivers
