#pragma once

#include "power.h"
namespace graphics
{
// Noop class for MESHTASTIC_EXCLUDE_SCREEN
class Screen
{
  public:
    enum FrameFocus : uint8_t {
        FOCUS_DEFAULT,
        FOCUS_PRESERVE,
        FOCUS_FAULT,
        FOCUS_TEXTMESSAGE,
        FOCUS_MODULE,
        FOCUS_CLOCK,
        FOCUS_SYSTEM,
    };

    explicit Screen(){};
    void setup() {}
    void setOn(bool) {}
    void doDeepSleep() {}
    void showSimpleBanner(const char *message, uint32_t durationMs = 0) {}
    void setFrames(FrameFocus focus) {}
};
} // namespace graphics

inline bool shouldWakeOnReceivedMessage()
{
    return false;
}
