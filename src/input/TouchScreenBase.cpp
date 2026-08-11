#include "TouchScreenBase.h"
#include "configuration.h"
#include "input/HapticFeedback.h"
#include "main.h"

#ifndef TOUCH_POLL_INTERVAL_IDLE
#define TOUCH_POLL_INTERVAL_IDLE 100
#endif

#ifndef TOUCH_POLL_INTERVAL_ACTIVE
#define TOUCH_POLL_INTERVAL_ACTIVE 20
#endif

#ifndef TOUCH_POLL_INTERVAL_RELEASE
#define TOUCH_POLL_INTERVAL_RELEASE 50
#endif

#ifndef TOUCH_POLL_INTERVAL_ACTIVE_FAST
#define TOUCH_POLL_INTERVAL_ACTIVE_FAST TOUCH_POLL_INTERVAL_ACTIVE
#endif

#ifndef TOUCH_POLL_INTERVAL_RELEASE_FAST
#define TOUCH_POLL_INTERVAL_RELEASE_FAST TOUCH_POLL_INTERVAL_RELEASE
#endif

#ifndef TOUCH_RELEASE_GRACE_MS
#define TOUCH_RELEASE_GRACE_MS 35
#endif

TouchScreenBase::TouchScreenBase(const char *name, uint16_t width, uint16_t height)
    : concurrency::OSThread(name), _display_width(width), _display_height(height), _recognizer(width, height),
      _targets(width, height), _originName(name)
{
}

void TouchScreenBase::init(bool hasTouch)
{
    if (hasTouch) {
        LOG_INFO("TouchScreen initialized: tap=12 lock=20 swipe=38 long=500ms stable=3");
        this->setInterval(TOUCH_POLL_INTERVAL_IDLE);
    } else {
        disable();
        this->setInterval(UINT_MAX);
    }
}

void TouchScreenBase::beginTouchFrame(uint32_t pageGeneration)
{
    _targets.beginFrame(pageGeneration);
}

void TouchScreenBase::markTouchFrameMapped()
{
    _targets.markFrameMapped();
}

bool TouchScreenBase::addTouchTarget(meshtastic::TouchRect rect, meshtastic::TouchTargetKind kind, uint32_t value,
                                     input_broker_event tapAction, input_broker_event longPressAction)
{
    return _targets.add(rect, kind, value, tapAction, longPressAction);
}

void TouchScreenBase::publishTouchFrame()
{
    _targets.publishFrame();
}

int32_t TouchScreenBase::runOnce()
{
    const uint32_t nowMs = millis();
    if (nowMs - _lastRun < 20)
        return 20;
    _lastRun = nowMs;

    TouchEvent e = {};
    e.touchEvent = static_cast<char>(TOUCH_ACTION_NONE);
    e.targetAction = INPUT_BROKER_NONE;
    this->setInterval(TOUCH_POLL_INTERVAL_IDLE);

    const bool fastTapMode = fastTapModeEnabled();
    const bool allowLongPress = longPressEnabled();
    int16_t x = _last_x;
    int16_t y = _last_y;
    const bool rawTouched = getTouch(x, y);
    const bool validTouched = rawTouched && meshtastic::TouchGestureRecognizer::transformCoordinates(
                                             x, y, _display_width, _display_height, config.display.flip_screen);
    bool touched = validTouched;

    if (touched) {
        _lastTouchSeenMs = nowMs;
        this->setInterval(fastTapMode ? TOUCH_POLL_INTERVAL_ACTIVE_FAST : TOUCH_POLL_INTERVAL_ACTIVE);
    } else if (!rawTouched && _touchedOld && nowMs - _lastTouchSeenMs < TOUCH_RELEASE_GRACE_MS) {
        touched = true;
        this->setInterval(fastTapMode ? TOUCH_POLL_INTERVAL_ACTIVE_FAST : TOUCH_POLL_INTERVAL_ACTIVE);
    } else {
        this->setInterval(fastTapMode ? TOUCH_POLL_INTERVAL_RELEASE_FAST : TOUCH_POLL_INTERVAL_RELEASE);
    }

    meshtastic::TouchGestureEvent gestureEvent;
    const bool emitted = _recognizer.update({x, y, nowMs, touched}, gestureEvent, allowLongPress);

    if (touched) {
        if (!_touchedOld) {
            hapticFeedback();
            _state = TOUCH_EVENT_OCCURRED;
            _targetCaptureStarted = _targets.capture(x, y);
        }
        if (_targetCaptureStarted)
            _targets.updateCapture(x, y);
        _last_x = x;
        _last_y = y;
    } else {
        _state = TOUCH_EVENT_CLEARED;
    }
    _touchedOld = touched;

    if (emitted) {
        switch (gestureEvent.gesture) {
        case meshtastic::TouchGesture::SwipeLeft:
            e.touchEvent = static_cast<char>(TOUCH_ACTION_LEFT);
            break;
        case meshtastic::TouchGesture::SwipeRight:
            e.touchEvent = static_cast<char>(TOUCH_ACTION_RIGHT);
            break;
        case meshtastic::TouchGesture::SwipeUp:
            e.touchEvent = static_cast<char>(TOUCH_ACTION_UP);
            break;
        case meshtastic::TouchGesture::SwipeDown:
            e.touchEvent = static_cast<char>(TOUCH_ACTION_DOWN);
            break;
        case meshtastic::TouchGesture::Tap:
            e.touchEvent = static_cast<char>(TOUCH_ACTION_TAP);
            break;
        case meshtastic::TouchGesture::LongPress:
            e.touchEvent = static_cast<char>(TOUCH_ACTION_LONG_PRESS);
            break;
        default:
            break;
        }

        _last_x = gestureEvent.x;
        _last_y = gestureEvent.y;
        if (gestureEvent.gesture == meshtastic::TouchGesture::Tap ||
            gestureEvent.gesture == meshtastic::TouchGesture::LongPress) {
            meshtastic::TouchTarget target{};
            const bool targetCaptureStarted = _targetCaptureStarted;
            const bool targetReleased =
                _targets.release(_last_x, _last_y, gestureEvent.gesture == meshtastic::TouchGesture::LongPress, target);
            _targetCaptureStarted = false;
            if (targetReleased) {
                e.targetAction = gestureEvent.gesture == meshtastic::TouchGesture::LongPress ? target.longPressAction
                                                                                              : target.tapAction;
                e.targetLongPress = gestureEvent.gesture == meshtastic::TouchGesture::LongPress;
                if (target.kind != meshtastic::TouchTargetKind::LegacyFallback) {
                    e.targetKind = static_cast<uint8_t>(target.kind);
                    e.targetValue = target.value;
                }
            } else if (targetCaptureStarted) {
                // A touch that leaves a registered target must not fall back to a generic tap.
                e.touchEvent = static_cast<char>(TOUCH_ACTION_NONE);
            }
        } else {
            _targets.cancelCapture();
            _targetCaptureStarted = false;
        }
    } else if (!touched && (!rawTouched || !validTouched)) {
        _targets.cancelCapture();
        _targetCaptureStarted = false;
    }

    if (e.touchEvent != TOUCH_ACTION_NONE) {
        e.source = this->_originName;
        e.x = static_cast<uint16_t>(_last_x);
        e.y = static_cast<uint16_t>(_last_y);
        onEvent(e);
    }

    return interval;
}

void TouchScreenBase::hapticFeedback()
{
#if defined(HAPTIC_FEEDBACK_PIN) || defined(HAS_DRV2605)
    if (::hapticFeedback)
        ::hapticFeedback->play(HapticEffect::NAVIGATION);
#endif
}

bool TouchScreenBase::fastTapModeEnabled() const
{
    return false;
}

bool TouchScreenBase::longPressEnabled() const
{
    return true;
}
