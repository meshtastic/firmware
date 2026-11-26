#pragma once

#include "InputBroker.h"
#include "concurrency/OSThread.h"
#include "mesh/NodeDB.h"
#include "time.h"

#ifndef TOUCH_RELEASE_GRACE_MS
#define TOUCH_RELEASE_GRACE_MS 35
#endif

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
    // The screen turned under a finger: its start and end points are in different frames, so the contact gives no
    // swipe, tap or long press. The next contact is read as usual
    void ignoreCurrentContact() { _ignoreContact = _touchedOld; }

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
    // When the latest touch began and ended, as the touch interrupt saw them, for boards whose interrupt line stays
    // low while a finger is down (TOUCH_INT_LEVEL). This thread can run hundreds of ms late (a slow screen redraw), and
    // a release seen that late would turn a tap into a long press. False when the board doesn't record them
    virtual bool touchEdgeTimes(uint32_t &downMs, uint32_t &upMs) { return false; }
    void resetTouchState();

    volatile TouchScreenBaseStateType _state = TOUCH_EVENT_CLEARED;
    volatile TouchScreenBaseEventType _action = TOUCH_ACTION_NONE;
    void hapticFeedback();

  protected:
    uint16_t _display_width;
    uint16_t _display_height;

  private:
    bool _touchedOld = false;     // previous touch state
    bool _ignoreContact = false;  // the screen turned during this contact (ignoreCurrentContact)
    int16_t _first_x, _last_x;    // horizontal swipe direction
    int16_t _first_y, _last_y;    // vertical swipe direction
    uint32_t _pressStartMs;       // when the current touch began; read via Throttle::hasElapsed()
    bool _pressDownTimed = false; // _pressStartMs is the interrupt's down time, so a release can be matched to it

    /// The interrupt's release time for the current touch, if its down edge is still the latest one
    bool timedRelease(uint32_t nowMs, uint32_t &upMs);

    // LONG_PRESS repeat suppression while one touch is held: the bool is the armed flag, the
    // deadline is read only while it is set. No value of the deadline can mean "unarmed".
    bool _longPressSuppressed;
    uint32_t _longPressSuppressUntilMs; // meaningful only while _longPressSuppressed
    uint32_t _lastTouchSeenMs;          // helps suppress brief touch-controller dropouts
    bool _tapped;                       // for DOUBLE_TAP
    uint32_t _lastRun = 0;              // helps suppress too fast consecutive runOnce() executions

    const char *_originName;
};
