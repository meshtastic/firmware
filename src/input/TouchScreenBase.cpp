#include "TouchScreenBase.h"

TouchScreenBase::TouchScreenBase(const char *name, uint16_t width, uint16_t height)
    : concurrency::OSThread(name), _tapped(false), _originName(name)
{
    // move a quarter over the screen to detect a "swipe"
    _touchThreshold_x = width / 4;
    _touchThreshold_y = height / 4;
}

void TouchScreenBase::init(bool hasTouch)
{
    if (hasTouch) {
        LOG_INFO("TouchScreen initialized %d %d\n", _touchThreshold_x, _touchThreshold_y);
        this->setInterval(100);
    } else {
        disable();
        this->setInterval(UINT_MAX);
    }
}

int32_t TouchScreenBase::runOnce()
{
    TouchEvent e;
    e.touchEvent = static_cast<char>(TOUCH_ACTION_NONE);

    // process touch events
    uint16_t x, y;
    bool touched = getTouch(x, y);
    if (touched) {
        this->setInterval(30);
        _last_x = x;
        _last_y = y;
    }
    if (touched != _touchedOld) {
        _state = (TouchScreenBaseStateType)(1 - _state);
        if (touched) {
            _state = TOUCH_EVENT_OCCURRED;
            _start = millis();
            _first_x = x;
            _first_y = y;
        } else {
            _state = TOUCH_EVENT_CLEARED;
            time_t duration = millis() - _start;
            x = _last_x;
            y = _last_y;
            this->setInterval(100);

            // compute distance
            int16_t dx = x - _first_x;
            int16_t dy = y - _first_y;
            uint16_t adx = abs(dx);
            uint16_t ady = abs(dy);

            // swipe horizontal
            if (adx > ady && adx > _touchThreshold_x) {
                if (0 > dx) { // swipe right to left
                    e.touchEvent = static_cast<char>(TOUCH_ACTION_LEFT);
                    LOG_DEBUG("action SWIPE: right to left\n");
                } else { // swipe left to right
                    e.touchEvent = static_cast<char>(TOUCH_ACTION_RIGHT);
                    LOG_DEBUG("action SWIPE: left to right\n");
                }
            }
            // swipe vertical
            else if (ady > adx && ady > _touchThreshold_y) {
                if (0 > dy) { // swipe bottom to top
                    e.touchEvent = static_cast<char>(TOUCH_ACTION_UP);
                    LOG_DEBUG("action SWIPE: bottom to top\n");
                } else { // swipe top to bottom
                    e.touchEvent = static_cast<char>(TOUCH_ACTION_DOWN);
                    LOG_DEBUG("action SWIPE: top to bottom\n");
                }
            }
            // tap
            else {
                if (duration > 400) {
                    e.touchEvent = static_cast<char>(TOUCH_ACTION_LONG_PRESS);
                    LOG_DEBUG("action LONG PRESS\n");
                } else {
                    if (_tapped) {
                        _tapped = false;
                        e.touchEvent = static_cast<char>(TOUCH_ACTION_DOUBLE_TAP);
                        LOG_DEBUG("action DOUBLE TAP\n");
                    } else {
                        _tapped = true;
                    }
                }
            }
            e.x = x;
            e.y = y;
        }
    }
    _touchedOld = touched;

    if (_tapped && (millis() - _start) > 350) {
        _tapped = false;
        e.touchEvent = static_cast<char>(TOUCH_ACTION_TAP);
        LOG_DEBUG("action TAP\n");
    }

    if (e.touchEvent != TOUCH_ACTION_NONE) {
        e.source = this->_originName;
        onEvent(e);
    }

    return interval;
}
