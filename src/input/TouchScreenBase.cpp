#include "TouchScreenBase.h"
#include "main.h"

#if defined(RAK14014) && !defined(MESHTASTIC_EXCLUDE_CANNEDMESSAGES)
#include "modules/CannedMessageModule.h"
#endif

#ifndef TIME_LONG_PRESS
#define TIME_LONG_PRESS 400
#endif

// Touch sampling cadence (milliseconds).
// Can be overridden by board variants for faster touch panels.
#ifndef TOUCH_POLL_INTERVAL_IDLE
#define TOUCH_POLL_INTERVAL_IDLE 100
#endif

#ifndef TOUCH_POLL_INTERVAL_ACTIVE
#define TOUCH_POLL_INTERVAL_ACTIVE 20
#endif

#ifndef TOUCH_POLL_INTERVAL_RELEASE
#define TOUCH_POLL_INTERVAL_RELEASE 50
#endif

// Faster cadence used for keyboard-like tap-heavy UIs.
#ifndef TOUCH_POLL_INTERVAL_ACTIVE_FAST
#define TOUCH_POLL_INTERVAL_ACTIVE_FAST TOUCH_POLL_INTERVAL_ACTIVE
#endif

#ifndef TOUCH_POLL_INTERVAL_RELEASE_FAST
#define TOUCH_POLL_INTERVAL_RELEASE_FAST TOUCH_POLL_INTERVAL_RELEASE
#endif

// Ignore very short "finger lifted" glitches from noisy touch controllers.
// A release is only accepted once we've seen no-touch for at least this duration.
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
    : concurrency::OSThread(name), _display_width(width), _display_height(height), _first_x(0), _last_x(0), _first_y(0),
      _last_y(0), _start(0), _lastTouchSeenMs(0), _tapped(false), _originName(name)
{
}

void TouchScreenBase::init(bool hasTouch)
{
    if (hasTouch) {
        LOG_INFO("TouchScreen initialized %d %d", TOUCH_THRESHOLD_X, TOUCH_THRESHOLD_Y);
        this->setInterval(TOUCH_POLL_INTERVAL_IDLE);
    } else {
        disable();
        this->setInterval(UINT_MAX);
    }
}

int32_t TouchScreenBase::runOnce()
{
    TouchEvent e;
    e.touchEvent = static_cast<char>(TOUCH_ACTION_NONE);
    const bool fastTapMode = fastTapModeEnabled();
    const bool allowLongPress = longPressEnabled();

    // process touch events
    int16_t x, y;
    bool touched = getTouch(x, y);
    if (x < 0 || y < 0) // T-deck can emit phantom touch events with a negative value when turning off the screen
        touched = false;
    if (touched) {
        _lastTouchSeenMs = millis();
        this->setInterval(fastTapMode ? TOUCH_POLL_INTERVAL_ACTIVE_FAST : TOUCH_POLL_INTERVAL_ACTIVE);
        _last_x = x;
        _last_y = y;
    } else if (_touchedOld && ((uint32_t)millis() - _lastTouchSeenMs) < TOUCH_RELEASE_GRACE_MS) {
        // Treat brief no-touch samples as continuous touch to preserve long-press detection.
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

            // compute distance
            int16_t dx = x - _first_x;
            int16_t dy = y - _first_y;
            uint16_t adx = abs(dx);
            uint16_t ady = abs(dy);

            // swipe horizontal
            if (adx > ady && adx > TOUCH_THRESHOLD_X) {
                if (0 > dx) { // swipe right to left
                    e.touchEvent = static_cast<char>(TOUCH_ACTION_LEFT);
                    LOG_DEBUG("action SWIPE: right to left");
                } else { // swipe left to right
                    e.touchEvent = static_cast<char>(TOUCH_ACTION_RIGHT);
                    LOG_DEBUG("action SWIPE: left to right");
                }
            }
            // swipe vertical
            else if (ady > adx && ady > TOUCH_THRESHOLD_Y) {
                if (0 > dy) { // swipe bottom to top
                    e.touchEvent = static_cast<char>(TOUCH_ACTION_UP);
                    LOG_DEBUG("action SWIPE: bottom to top");
                } else { // swipe top to bottom
                    e.touchEvent = static_cast<char>(TOUCH_ACTION_DOWN);
                    LOG_DEBUG("action SWIPE: top to bottom");
                }
            }
            // tap
            else {
                if (duration > 0 && (duration < TIME_LONG_PRESS || !allowLongPress)) {
                    if (_tapped) {
                        _tapped = false;
                    } else {
                        _tapped = true;
                    }
                } else {
                    _tapped = false;
                }
            }
        }
    }
    _touchedOld = touched;

#if defined RAK14014
    // Speed up the processing speed of the keyboard in virtual keyboard mode
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
    // fire TAP event when no 2nd tap occurred within time
    if (_tapped) {
        _tapped = false;
        e.touchEvent = static_cast<char>(TOUCH_ACTION_TAP);
        LOG_DEBUG("action TAP(%d/%d)", _last_x, _last_y);
    }
#endif

    // fire LONG_PRESS event without the need for release
    if (allowLongPress && touched && (time_t(millis()) - _start) > TIME_LONG_PRESS) {
        // tricky: prevent reoccurring events and another touch event when releasing
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
}

void TouchScreenBase::hapticFeedback()
{
#ifdef T_WATCH_S3
    drv.setWaveform(0, 75);
    drv.setWaveform(1, 0); // end waveform
    drv.go();
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
