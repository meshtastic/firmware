#include "TrackballInterruptBase.h"
#include "configuration.h"

TrackballInterruptBase::TrackballInterruptBase(const char *name) : concurrency::OSThread(name), _originName(name) {}

void TrackballInterruptBase::init(uint8_t pinDown, uint8_t pinUp, uint8_t pinLeft, uint8_t pinRight, uint8_t pinPress,
                                  input_broker_event eventDown, input_broker_event eventUp, input_broker_event eventLeft,
                                  input_broker_event eventRight, input_broker_event eventPressed, void (*onIntDown)(),
                                  void (*onIntUp)(), void (*onIntLeft)(), void (*onIntRight)(), void (*onIntPress)())
{
    this->_pinDown = pinDown;
    this->_pinUp = pinUp;
    this->_pinLeft = pinLeft;
    this->_pinRight = pinRight;
    this->_pinPress = pinPress;
    this->_eventDown = eventDown;
    this->_eventUp = eventUp;
    this->_eventLeft = eventLeft;
    this->_eventRight = eventRight;
    this->_eventPressed = eventPressed;

    if (pinPress != 255) {
        pinMode(pinPress, INPUT_PULLUP);
        attachInterrupt(pinPress, onIntPress, TB_DIRECTION);
    }
    if (this->_pinDown != 255) {
        pinMode(this->_pinDown, INPUT_PULLUP);
        attachInterrupt(this->_pinDown, onIntDown, TB_DIRECTION);
    }
    if (this->_pinUp != 255) {
        pinMode(this->_pinUp, INPUT_PULLUP);
        attachInterrupt(this->_pinUp, onIntUp, TB_DIRECTION);
    }
    if (this->_pinLeft != 255) {
        pinMode(this->_pinLeft, INPUT_PULLUP);
        attachInterrupt(this->_pinLeft, onIntLeft, TB_DIRECTION);
    }
    if (this->_pinRight != 255) {
        pinMode(this->_pinRight, INPUT_PULLUP);
        attachInterrupt(this->_pinRight, onIntRight, TB_DIRECTION);
    }

    LOG_DEBUG("Trackball GPIO initialized (%d, %d, %d, %d, %d)", this->_pinUp, this->_pinDown, this->_pinLeft, this->_pinRight,
              pinPress);

    this->setInterval(100);
}

int32_t TrackballInterruptBase::runOnce()
{
    InputEvent e;
    e.inputEvent = INPUT_BROKER_NONE;
#if defined(T_DECK) // T-deck gets a super-simple debounce on trackball
    if (this->action == TB_ACTION_PRESSED) {
        // LOG_DEBUG("Trackball event Press");
        e.inputEvent = this->_eventPressed;
    } else if (this->action == TB_ACTION_UP && lastEvent == TB_ACTION_UP) {
        // LOG_DEBUG("Trackball event UP");
        e.inputEvent = this->_eventUp;
    } else if (this->action == TB_ACTION_DOWN && lastEvent == TB_ACTION_DOWN) {
        // LOG_DEBUG("Trackball event DOWN");
        e.inputEvent = this->_eventDown;
    } else if (this->action == TB_ACTION_LEFT && lastEvent == TB_ACTION_LEFT) {
        // LOG_DEBUG("Trackball event LEFT");
        e.inputEvent = this->_eventLeft;
    } else if (this->action == TB_ACTION_RIGHT && lastEvent == TB_ACTION_RIGHT) {
        // LOG_DEBUG("Trackball event RIGHT");
        e.inputEvent = this->_eventRight;
    }
#else
    if (this->action == TB_ACTION_PRESSED && !digitalRead(_pinPress)) {
        // LOG_DEBUG("Trackball event Press");
        e.inputEvent = this->_eventPressed;
    } else if (this->action == TB_ACTION_UP && !digitalRead(_pinUp)) {
        // LOG_DEBUG("Trackball event UP");
        e.inputEvent = this->_eventUp;
    } else if (this->action == TB_ACTION_DOWN && !digitalRead(_pinDown)) {
        // LOG_DEBUG("Trackball event DOWN");
        e.inputEvent = this->_eventDown;
    } else if (this->action == TB_ACTION_LEFT && !digitalRead(_pinLeft)) {
        // LOG_DEBUG("Trackball event LEFT");
        e.inputEvent = this->_eventLeft;
    } else if (this->action == TB_ACTION_RIGHT && !digitalRead(_pinRight)) {
        // LOG_DEBUG("Trackball event RIGHT");
        e.inputEvent = this->_eventRight;
    }
#endif

    if (e.inputEvent != INPUT_BROKER_NONE) {
        e.source = this->_originName;
        e.kbchar = 0x00;
        this->notifyObservers(&e);
    }
    lastEvent = action;
    this->action = TB_ACTION_NONE;

    return 100;
}

void TrackballInterruptBase::intPressHandler()
{
    this->action = TB_ACTION_PRESSED;
}

void TrackballInterruptBase::intDownHandler()
{
    this->action = TB_ACTION_DOWN;
}

void TrackballInterruptBase::intUpHandler()
{
    this->action = TB_ACTION_UP;
}

void TrackballInterruptBase::intLeftHandler()
{
    this->action = TB_ACTION_LEFT;
}

void TrackballInterruptBase::intRightHandler()
{
    this->action = TB_ACTION_RIGHT;
}
