#pragma once

#include <stdint.h>

constexpr bool externalNotificationDeadlineExpired(uint32_t deadlineMs, uint32_t nowMs)
{
    return deadlineMs != UINT32_MAX && static_cast<int32_t>(nowMs - deadlineMs) > 0;
}

constexpr bool shouldDriveHapticNotification(bool hapticAlert, bool notificationActive)
{
    return hapticAlert && notificationActive;
}
