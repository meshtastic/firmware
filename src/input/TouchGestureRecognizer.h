#pragma once

#include <stdint.h>

namespace meshtastic {

struct TouchSample {
    int16_t x;
    int16_t y;
    uint32_t timestamp;
    bool touched;
};

enum class TouchGesture : uint8_t {
    None,
    Tap,
    LongPress,
    SwipeLeft,
    SwipeRight,
    SwipeUp,
    SwipeDown,
};

struct TouchGestureEvent {
    TouchGesture gesture = TouchGesture::None;
    int16_t x = 0;
    int16_t y = 0;
    uint32_t duration = 0;
};

class TouchGestureRecognizer
{
  public:
    TouchGestureRecognizer(uint16_t width, uint16_t height);

    bool update(const TouchSample &sample, TouchGestureEvent &event, bool allowLongPress = true);
    void reset();
    void cancel();

    static bool transformCoordinates(int16_t &x, int16_t &y, uint16_t width, uint16_t height, bool flipScreen,
                                     int16_t offsetX = 0, int16_t offsetY = 0, float scaleX = 1.0f,
                                     float scaleY = 1.0f);

  private:
    enum class State : uint8_t { Idle, Tracking, LongPressed, Cancelled };

    static constexpr uint8_t FILTER_SAMPLES = 3;
    static constexpr uint8_t STABLE_SAMPLES_REQUIRED = 3;
    static constexpr int16_t MAX_JUMP = 80;
    static constexpr int16_t TAP_DEAD_ZONE = 12;
    static constexpr int16_t LOCK_DISTANCE = 20;
    static constexpr int16_t SWIPE_DISTANCE = 38;
    static constexpr int16_t LONG_PRESS_MOVE = 14;
    static constexpr uint32_t LONG_PRESS_MS = 500;

    bool acceptSample(int16_t rawX, int16_t rawY, int16_t &filteredX, int16_t &filteredY);
    bool lockDirection(int16_t dx, int16_t dy);
    TouchGesture directionFor(int16_t dx, int16_t dy) const;
    void emit(TouchGesture gesture, uint32_t duration, TouchGestureEvent &event);
    static int16_t median(const int16_t *values, uint8_t count);

    uint16_t width;
    uint16_t height;
    State state = State::Idle;
    bool directionLocked = false;
    // Once filtered motion exceeds the tap dead zone, this press must not be
    // converted back into a tap.
    bool pressMoved = false;
    bool longPressRejected = false;
    uint8_t stableSamples = 0;
    uint32_t startTime = 0;
    int16_t startRawX = 0;
    int16_t startRawY = 0;
    int16_t lastX = 0;
    int16_t lastY = 0;
    int32_t gestureSumX = 0;
    int32_t gestureSumY = 0;
    int32_t maxFilteredDistance = 0;
    int16_t maxRawDistance = 0;
    int16_t maxRawAbsX = 0;
    int16_t maxRawAbsY = 0;
    TouchGesture lockedGesture = TouchGesture::None;
    bool haveRaw = false;
    bool pendingJump = false;
    bool rejectedJump = false;
    bool confirmedJump = false;
    int16_t lastRawX = 0;
    int16_t lastRawY = 0;
    int16_t pendingX = 0;
    int16_t pendingY = 0;
    int16_t filterX[FILTER_SAMPLES] = {};
    int16_t filterY[FILTER_SAMPLES] = {};
    int16_t lastFilteredX = 0;
    int16_t lastFilteredY = 0;
    uint8_t filterCount = 0;
    uint8_t filterIndex = 0;
    bool haveFiltered = false;
};

} // namespace meshtastic
