#pragma once

#include "InputBroker.h"
#include "concurrency/OSThread.h"
#include "mesh/NodeDB.h"
#include "time.h"

typedef struct _TouchEvent {
    const char *source;
    char touchEvent;
    uint16_t x;
    uint16_t y;
} TouchEvent;

class TouchScreenBase : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit TouchScreenBase(const char *name, uint16_t width, uint16_t height);
    void init(bool hasTouch);

  protected:
    enum TouchScreenBaseStateType { TOUCH_EVENT_OCCURRED, TOUCH_EVENT_CLEARED };

    enum TouchScreenBaseEventType {
        TOUCH_ACTION_NONE,
        TOUCH_ACTION_UP,
        TOUCH_ACTION_DOWN,
        TOUCH_ACTION_LEFT,
        TOUCH_ACTION_RIGHT,
        TOUCH_ACTION_TAP,
        TOUCH_ACTION_LONG_PRESS
    };

    virtual int32_t runOnce() override;

    virtual bool getTouch(int16_t &x, int16_t &y) = 0;
    virtual void onEvent(const TouchEvent &event) = 0;
    virtual bool fastTapModeEnabled() const;
    virtual bool longPressEnabled() const;

    volatile TouchScreenBaseStateType _state = TOUCH_EVENT_CLEARED;
    volatile TouchScreenBaseEventType _action = TOUCH_ACTION_NONE;
    void hapticFeedback();

  protected:
    uint16_t _display_width;
    uint16_t _display_height;

  private:
    bool _touchedOld = false;  // previous touch state
    int16_t _first_x, _last_x; // horizontal swipe direction
    int16_t _first_y, _last_y; // vertical swipe direction
    // When the current touch began. A past event time, so every "how long has the finger been down"
    // question goes through Throttle::hasElapsed(), which is wrap-correct and gets the full ~49.7
    // day range. This and the two fields below used to be a single `time_t _start` that doubled as a
    // suppression deadline; the LONG_PRESS block in runOnce() records what that cost.
    uint32_t _pressStartMs;

    // Repeat suppression for LONG_PRESS while one touch is held. Deliberately two fields: the bool
    // answers "is suppression armed", the deadline answers "has it expired". Packing both into one
    // timestamp is what made the old code wrong, and no single value can stand for "unarmed" here -
    // Throttle::deadlinePassed() reads 0 as long past below ~24.8 days of uptime and as far future
    // above it.
    bool _longPressSuppressed;
    uint32_t _longPressSuppressUntilMs; // meaningful only while _longPressSuppressed
    uint32_t _lastTouchSeenMs;          // helps suppress brief touch-controller dropouts
    bool _tapped;                       // for DOUBLE_TAP
    uint32_t _lastRun = 0;              // helps suppress too fast consecutive runOnce() executions

    const char *_originName;
};
