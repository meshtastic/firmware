#pragma once

#include "InputBroker.h"
#include "TouchGestureRecognizer.h"
#include "TouchTargetRegistry.h"
#include "concurrency/OSThread.h"
#include "mesh/NodeDB.h"

typedef struct _TouchEvent {
    const char *source;
    char touchEvent;
    uint16_t x;
    uint16_t y;
    input_broker_event targetAction;
    uint8_t targetKind;
    uint32_t targetValue;
    uint8_t targetLongPress;
} TouchEvent;

class TouchScreenBase : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit TouchScreenBase(const char *name, uint16_t width, uint16_t height);
    void init(bool hasTouch);
    void beginTouchFrame(uint32_t pageGeneration);
    void markTouchFrameMapped();
    bool addTouchTarget(meshtastic::TouchRect rect, meshtastic::TouchTargetKind kind, uint32_t value,
                        input_broker_event tapAction, input_broker_event longPressAction = INPUT_BROKER_NONE);
    void publishTouchFrame();

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
    bool _touchedOld = false;
    int16_t _last_x = 0;
    int16_t _last_y = 0;
    uint32_t _lastTouchSeenMs = 0;
    uint32_t _lastRun = 0;
    meshtastic::TouchGestureRecognizer _recognizer;
    meshtastic::TouchTargetRegistry _targets;
    bool _targetCaptureStarted = false;

    const char *_originName;
};
