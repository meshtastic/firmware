#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace t_deck_max
{

enum class TouchReportKind : uint8_t { None, Coordinate, Key };

enum class MaxTouchKey : uint8_t {
    Left = 0,
    Center = 1,
    Right = 2,
    Invalid = 0xFF,
};

struct TouchReport {
    TouchReportKind kind = TouchReportKind::None;
    uint16_t x = 0;
    uint16_t y = 0;
    uint8_t keyId = 0;
    bool pressed = false;
};

inline constexpr const char *MAX_TOUCH_KEY_SOURCE = "t-deck-max-touch-key";

inline MaxTouchKey maxTouchKeyForId(uint8_t keyId)
{
    switch (keyId) {
    case 0:
        return MaxTouchKey::Left;
    case 1:
        return MaxTouchKey::Center;
    case 2:
        return MaxTouchKey::Right;
    default:
        return MaxTouchKey::Invalid;
    }
}

inline bool isMaxTouchKeySource(const char *source)
{
    return source != nullptr && std::strcmp(source, MAX_TOUCH_KEY_SOURCE) == 0;
}

inline bool isSafeMaxMenuBackLabel(const char *label)
{
    return label != nullptr && (std::strcmp(label, "Back") == 0 || std::strcmp(label, "No") == 0 ||
                                std::strcmp(label, "Reject") == 0 || std::strcmp(label, "Keep licensed") == 0);
}

inline TouchReport decodeTouchReport(const uint8_t *buffer, std::size_t length, uint16_t displayWidth,
                                     uint16_t displayHeight)
{
    TouchReport report;
    if (buffer == nullptr || length != 9 || displayWidth == 0 || displayHeight == 0 || buffer[2] != 0xFF)
        return report;

    const uint8_t fingerCount = buffer[3] & 0x0F;
    const uint8_t keyCount = buffer[3] >> 4;
    if (fingerCount + keyCount != 1)
        return report;

    uint16_t checksum = 0x55;
    for (uint8_t index = 4; index < 9; ++index)
        checksum = static_cast<uint16_t>(checksum + buffer[index]);
    const uint16_t reportedChecksum = static_cast<uint16_t>(buffer[0]) |
                                       (static_cast<uint16_t>(buffer[1]) << 8);
    if (checksum != reportedChecksum)
        return report;

    if (keyCount == 1) {
        const uint8_t keyId = buffer[8] & 0x0F;
        if (maxTouchKeyForId(keyId) == MaxTouchKey::Invalid)
            return report;

        report.kind = TouchReportKind::Key;
        report.keyId = keyId;
        report.pressed = (buffer[8] >> 4) != 0;
        return report;
    }

    if ((buffer[8] >> 4) == 0)
        return report;

    const uint16_t x = buffer[4] + ((static_cast<uint16_t>(buffer[7]) & 0x0F) << 8);
    const uint16_t y = buffer[5] + ((static_cast<uint16_t>(buffer[7]) & 0xF0) << 4);
    if (x >= displayWidth || y >= displayHeight)
        return report;

    report.kind = TouchReportKind::Coordinate;
    report.x = x;
    report.y = y;
    return report;
}

} // namespace t_deck_max
