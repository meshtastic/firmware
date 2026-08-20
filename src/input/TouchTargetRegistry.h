#pragma once

#include "InputBroker.h"
#include "concurrency/LockGuard.h"
#include <stdint.h>

namespace meshtastic {

struct TouchRect {
    int16_t left;
    int16_t top;
    int16_t right;
    int16_t bottom;

    bool contains(int16_t x, int16_t y) const { return x >= left && x < right && y >= top && y < bottom; }
};

enum class TouchTargetKind : uint8_t {
    None,
    LegacyFallback,
    NavigationPrevious,
    NavigationNext,
    Back,
    MenuOption,
    NodeRow,
    MessageRow,
    EmoteRow,
    NotificationOption,
    KeyboardKey,
    Confirm,
    Cancel,
};

struct TouchTarget {
    TouchRect rect{};
    TouchTargetKind kind = TouchTargetKind::None;
    uint32_t value = 0;
    input_broker_event tapAction = INPUT_BROKER_NONE;
    input_broker_event longPressAction = INPUT_BROKER_NONE;
    uint32_t pageGeneration = 0;
};

class TouchTargetRegistry
{
  public:
    static constexpr uint8_t MAX_TARGETS = 64;

    TouchTargetRegistry(uint16_t width, uint16_t height);
    void beginFrame(uint32_t pageGeneration);
    void clear();
    void markFrameMapped();
    bool addFullScreen(input_broker_event tapAction, input_broker_event longPressAction = INPUT_BROKER_NONE);
    bool add(TouchRect rect, TouchTargetKind kind, uint32_t value, input_broker_event tapAction,
             input_broker_event longPressAction = INPUT_BROKER_NONE);
    void publishFrame();
    bool hasTargets() const;
    bool isStagingFrameMapped() const;
    bool isFrameMapped() const;
    bool hitTest(int16_t x, int16_t y, TouchTarget &out) const;
    bool capture(int16_t x, int16_t y);
    bool updateCapture(int16_t x, int16_t y);
    bool release(int16_t x, int16_t y, bool longPress, TouchTarget &out);
    void cancelCapture();

  private:
    bool clip(TouchRect &rect) const;
    bool hitTestLocked(int16_t x, int16_t y, uint8_t &index) const;

    uint16_t width;
    uint16_t height;
    uint32_t stagingGeneration = 0;
    uint32_t activeGeneration = 0;
    TouchTarget stagingTargets[MAX_TARGETS] = {};
    TouchTarget activeTargets[MAX_TARGETS] = {};
    uint8_t stagingCount = 0;
    uint8_t activeCount = 0;
    bool stagingMapped = false;
    bool activeMapped = false;
    int8_t captured = -1;
    bool captureValid = false;
    mutable concurrency::Lock lock;
};

} // namespace meshtastic
