#include "TrackballInterruptBase.h"
#include "configuration.h"

TrackballInterruptBase::TrackballInterruptBase(const char *name) : concurrency::OSThread(name), _originName(name) {}

void TrackballInterruptBase::init(uint8_t pinDown, uint8_t pinUp, uint8_t pinLeft, uint8_t pinRight, uint8_t pinPress,
                                  char eventDown, char eventUp, char eventLeft, char eventRight, char eventPressed,
                                  void (*onIntDown)(), void (*onIntUp)(), void (*onIntLeft)(), void (*onIntRight)(),
                                  void (*onIntPress)())
{
    this->_pinDown = pinDown;
    this->_pinUp = pinUp;
    this->_pinLeft = pinLeft;
    this->_pinRight = pinRight;
    this->_eventDown = eventDown;
    this->_eventUp = eventUp;
    this->_eventLeft = eventLeft;
    this->_eventRight = eventRight;
    this->_eventPressed = eventPressed;

    pinMode(pinPress, INPUT_PULLUP);
    pinMode(this->_pinDown, INPUT_PULLUP);
    pinMode(this->_pinUp, INPUT_PULLUP);
    pinMode(this->_pinLeft, INPUT_PULLUP);
    pinMode(this->_pinRight, INPUT_PULLUP);

    attachInterrupt(pinPress, onIntPress, RISING);
    attachInterrupt(this->_pinDown, onIntDown, RISING);
    attachInterrupt(this->_pinUp, onIntUp, RISING);
    attachInterrupt(this->_pinLeft, onIntLeft, RISING);
    attachInterrupt(this->_pinRight, onIntRight, RISING);

    LOG_DEBUG("Trackball GPIO initialized (%d, %d, %d, %d, %d)\n", this->_pinUp, this->_pinDown, this->_pinLeft, this->_pinRight,
              pinPress);

    this->setInterval(100);
}

int32_t TrackballInterruptBase::runOnce()
{
    InputEvent e;
    e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;

    if (this->action == TB_ACTION_PRESSED) {
        // LOG_DEBUG("Trackball event Press\n");
        e.inputEvent = this->_eventPressed;
    } else if (this->action == TB_ACTION_UP) {
        // LOG_DEBUG("Trackball event UP\n");
        e.inputEvent = this->_eventUp;
    } else if (this->action == TB_ACTION_DOWN) {
        // LOG_DEBUG("Trackball event DOWN\n");
        e.inputEvent = this->_eventDown;
    } else if (this->action == TB_ACTION_LEFT) {
        // LOG_DEBUG("Trackball event LEFT\n");
        e.inputEvent = this->_eventLeft;
    } else if (this->action == TB_ACTION_RIGHT) {
        // LOG_DEBUG("Trackball event RIGHT\n");
        e.inputEvent = this->_eventRight;
    }

    if (e.inputEvent != meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE) {
        e.source = this->_originName;
        e.kbchar = 0x00;
        this->notifyObservers(&e);
    }

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
