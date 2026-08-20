#pragma once

#include "configuration.h"
#include "InputBroker.h"
#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1)
#include "TouchGestureRecognizer.h"
#include "TouchTargetRegistry.h"
#endif
#include "concurrency/OSThread.h"
#include "mesh/NodeDB.h"
#include "time.h"

typedef struct _TouchEvent {
    const char *source;
    char touchEvent;
    uint16_t x;
    uint16_t y;
#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1)
    input_broker_event targetAction;
    uint8_t targetKind;
    uint32_t targetValue;
    uint8_t targetLongPress;
#endif
} TouchEvent;

class TouchScreenBase : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit TouchScreenBase(const char *name, uint16_t width, uint16_t height);
    void init(bool hasTouch);
#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1)
    void beginTouchFrame(uint32_t pageGeneration);
    void markTouchFrameMapped();
    bool addTouchTarget(meshtastic::TouchRect rect, meshtastic::TouchTargetKind kind, uint32_t value,
                        input_broker_event tapAction, input_broker_event longPressAction = INPUT_BROKER_NONE);
    void publishTouchFrame();
#endif

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
#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1)
    bool _touchedOld = false;
    int16_t _last_x = 0;
    int16_t _last_y = 0;
    uint32_t _lastTouchSeenMs = 0;
    uint32_t _lastRun = 0;
#else
    bool _touchedOld = false;
    int16_t _first_x = 0;
    int16_t _last_x = 0;
    int16_t _first_y = 0;
    int16_t _last_y = 0;
    time_t _start = 0;
    uint32_t _lastTouchSeenMs = 0;
    bool _tapped = false;
    uint32_t _lastRun = 0;
#endif
#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1)
    meshtastic::TouchGestureRecognizer _recognizer;
    meshtastic::TouchTargetRegistry _targets;
    bool _targetCaptureStarted = false;
#endif

    const char *_originName;
};
