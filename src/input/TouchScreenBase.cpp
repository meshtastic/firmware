#include "TouchScreenBase.h"
#include "configuration.h"
#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1)
#include "input/HapticFeedback.h"
#endif
#include "main.h"

#if defined(RAK14014) && !defined(MESHTASTIC_EXCLUDE_CANNEDMESSAGES)
#include "modules/CannedMessageModule.h"
#endif

#ifndef TIME_LONG_PRESS
#define TIME_LONG_PRESS 400
#endif

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

// move a minimum distance over the screen to detect a "swipe"
#ifndef TOUCH_THRESHOLD_X
#define TOUCH_THRESHOLD_X 30
#endif

#ifndef TOUCH_THRESHOLD_Y
#define TOUCH_THRESHOLD_Y 20
#endif

TouchScreenBase::TouchScreenBase(const char *name, uint16_t width, uint16_t height)
#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1)
    : concurrency::OSThread(name), _display_width(width), _display_height(height), _recognizer(width, height),
      _targets(width, height), _originName(name)
#else
    : concurrency::OSThread(name), _display_width(width), _display_height(height), _originName(name)
#endif
{
}

void TouchScreenBase::init(bool hasTouch)
{
    if (hasTouch) {
#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1)
        LOG_INFO("TouchScreen initialized: tap=12 lock=20 swipe=38 long=500ms stable=3");
#else
        LOG_INFO("TouchScreen initialized %d %d", TOUCH_THRESHOLD_X, TOUCH_THRESHOLD_Y);
#endif
        this->setInterval(TOUCH_POLL_INTERVAL_IDLE);
    } else {
        disable();
        this->setInterval(UINT_MAX);
    }
}

#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1)
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
#endif

int32_t TouchScreenBase::runOnce()
{
#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1)
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

#else
    uint32_t nowMs = millis();
    if (nowMs - _lastRun < 20) { // suppress too fast consecutive runOnce() executions
        return 20;
    }
    _lastRun = nowMs;
    TouchEvent e;
    e.touchEvent = static_cast<char>(TOUCH_ACTION_NONE);
    this->setInterval(TOUCH_POLL_INTERVAL_IDLE);
    const bool fastTapMode = fastTapModeEnabled();
    const bool allowLongPress = longPressEnabled();

    int16_t x, y;
    bool touched = getTouch(x, y);
    if (x < 0 || y < 0)
        touched = false;
    if (touched) {
        _lastTouchSeenMs = millis();
        this->setInterval(fastTapMode ? TOUCH_POLL_INTERVAL_ACTIVE_FAST : TOUCH_POLL_INTERVAL_ACTIVE);
        _last_x = x;
        _last_y = y;
    } else if (_touchedOld && ((uint32_t)millis() - _lastTouchSeenMs) < TOUCH_RELEASE_GRACE_MS) {
        touched = true;
    }
    if (touched != _touchedOld) {
        if (touched) {
            hapticFeedback();
            _state = TOUCH_EVENT_OCCURRED;
            _start = millis();
            _first_x = x;
            _first_y = y;
        } else {
            _state = TOUCH_EVENT_CLEARED;
            time_t duration = millis() - _start;
            x = _last_x;
            y = _last_y;
            this->setInterval(fastTapMode ? TOUCH_POLL_INTERVAL_RELEASE_FAST : TOUCH_POLL_INTERVAL_RELEASE);

            int16_t dx = x - _first_x;
            int16_t dy = y - _first_y;
            uint16_t adx = abs(dx);
            uint16_t ady = abs(dy);

            if (adx > ady && adx > TOUCH_THRESHOLD_X) {
                if (0 > dx) {
                    e.touchEvent = static_cast<char>(TOUCH_ACTION_LEFT);
                    LOG_DEBUG("action SWIPE: right to left");
                } else {
                    e.touchEvent = static_cast<char>(TOUCH_ACTION_RIGHT);
                    LOG_DEBUG("action SWIPE: left to right");
                }
            } else if (ady > adx && ady > TOUCH_THRESHOLD_Y) {
                if (0 > dy) {
                    e.touchEvent = static_cast<char>(TOUCH_ACTION_UP);
                    LOG_DEBUG("action SWIPE: bottom to top");
                } else {
                    e.touchEvent = static_cast<char>(TOUCH_ACTION_DOWN);
                    LOG_DEBUG("action SWIPE: top to bottom");
                }
            } else {
                if (duration > 0 && (duration < TIME_LONG_PRESS || !allowLongPress)) {
                    if (_tapped)
                        _tapped = false;
                    else
                        _tapped = true;
                } else {
                    _tapped = false;
                }
            }
        }
    }
    _touchedOld = touched;

#if defined RAK14014
    auto state = cannedMessageModule->getRunState();
    if (state == CANNED_MESSAGE_RUN_STATE_FREETEXT) {
        if (_tapped) {
            _tapped = false;
            e.touchEvent = static_cast<char>(TOUCH_ACTION_TAP);
            LOG_DEBUG("action TAP(%d/%d)", _last_x, _last_y);
        }
    } else {
        if (_tapped && (time_t(millis()) - _start) > TIME_LONG_PRESS - 50) {
            _tapped = false;
            e.touchEvent = static_cast<char>(TOUCH_ACTION_TAP);
            LOG_DEBUG("action TAP(%d/%d)", _last_x, _last_y);
        }
    }
#else
    if (_tapped) {
        _tapped = false;
        e.touchEvent = static_cast<char>(TOUCH_ACTION_TAP);
        LOG_DEBUG("action TAP(%d/%d)", _last_x, _last_y);
    }
#endif

    if (allowLongPress && touched && (time_t(millis()) - _start) > TIME_LONG_PRESS) {
        _start = millis() + 30000;
        e.touchEvent = static_cast<char>(TOUCH_ACTION_LONG_PRESS);
        LOG_DEBUG("action LONG PRESS(%d/%d)", _last_x, _last_y);
    }

    if (e.touchEvent != TOUCH_ACTION_NONE) {
        e.source = this->_originName;
        e.x = _last_x;
        e.y = _last_y;
        onEvent(e);
    }

    return interval;
#endif
}

void TouchScreenBase::hapticFeedback()
{
#if defined(T_DECK_MAX) || defined(_VARIANT_T_DECK_PRO_V1_1)
#if defined(HAPTIC_FEEDBACK_PIN) || defined(HAS_DRV2605)
    if (::hapticFeedback)
        ::hapticFeedback->play(HapticEffect::NAVIGATION);
#endif
#else
#ifdef T_WATCH_S3
    drv.setWaveform(0, 75);
    drv.setWaveform(1, 0); // end waveform
    drv.go();
#endif
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
