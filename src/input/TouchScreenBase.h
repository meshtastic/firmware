#pragma once

#include "InputBroker.h"
#include "concurrency/OSThread.h"
#include "mesh/NodeDB.h"

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
        TOUCH_ACTION_DOUBLE_TAP,
        TOUCH_ACTION_LONG_PRESS
    };

    virtual int32_t runOnce() override;

    virtual bool getTouch(int16_t &x, int16_t &y) = 0;
    virtual void onEvent(const TouchEvent &event) = 0;

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
    time_t _start;             // for LONG_PRESS
    bool _tapped;              // for DOUBLE_TAP

    const char *_originName;
};
