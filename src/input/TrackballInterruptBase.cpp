#include "TrackballInterruptBase.h"
#include "configuration.h"

TrackballInterruptBase::TrackballInterruptBase(const char *name)
{
    this->_originName = name;
}

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
}

void TrackballInterruptBase::intPressHandler()
{
    InputEvent e;
    e.source = this->_originName;
    e.inputEvent = this->_eventPressed;
    this->notifyObservers(&e);
}

void TrackballInterruptBase::intDownHandler()
{
    InputEvent e;
    e.source = this->_originName;
    e.inputEvent = this->_eventDown;
    this->notifyObservers(&e);
}

void TrackballInterruptBase::intUpHandler()
{
    InputEvent e;
    e.source = this->_originName;
    e.inputEvent = this->_eventUp;
    this->notifyObservers(&e);
}

void TrackballInterruptBase::intLeftHandler()
{
    InputEvent e;
    e.source = this->_originName;
    e.inputEvent = this->_eventLeft;
    this->notifyObservers(&e);
}

void TrackballInterruptBase::intRightHandler()
{
    InputEvent e;
    e.source = this->_originName;
    e.inputEvent = this->_eventRight;
    this->notifyObservers(&e);
}
