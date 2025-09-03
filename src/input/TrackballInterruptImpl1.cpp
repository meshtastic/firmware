#include "TrackballInterruptImpl1.h"
#include "InputBroker.h"
#include "configuration.h"

TrackballInterruptImpl1 *trackballInterruptImpl1;

TrackballInterruptImpl1::TrackballInterruptImpl1() : TrackballInterruptBase("trackball1") {}

void TrackballInterruptImpl1::init(uint8_t pinDown, uint8_t pinUp, uint8_t pinLeft, uint8_t pinRight, uint8_t pinPress)
{
    input_broker_event eventDown = INPUT_BROKER_DOWN;
    input_broker_event eventUp = INPUT_BROKER_UP;
    input_broker_event eventLeft = INPUT_BROKER_LEFT;
    input_broker_event eventRight = INPUT_BROKER_RIGHT;
    input_broker_event eventPressed = INPUT_BROKER_SELECT;
    input_broker_event eventPressedLong = INPUT_BROKER_SELECT_LONG;

    TrackballInterruptBase::init(pinDown, pinUp, pinLeft, pinRight, pinPress, eventDown, eventUp, eventLeft, eventRight,
                                 eventPressed, eventPressedLong, TrackballInterruptImpl1::handleIntDown,
                                 TrackballInterruptImpl1::handleIntUp, TrackballInterruptImpl1::handleIntLeft,
                                 TrackballInterruptImpl1::handleIntRight, TrackballInterruptImpl1::handleIntPressed);
    inputBroker->registerSource(this);
}

void TrackballInterruptImpl1::handleIntDown()
{
    if (TB_DIRECTION == RISING || millis() > trackballInterruptImpl1->lastTime + 10) {
        trackballInterruptImpl1->lastTime = millis();
        trackballInterruptImpl1->intDownHandler();
        trackballInterruptImpl1->setIntervalFromNow(20);
    }
}
void TrackballInterruptImpl1::handleIntUp()
{
    if (TB_DIRECTION == RISING || millis() > trackballInterruptImpl1->lastTime + 10) {
        trackballInterruptImpl1->lastTime = millis();
        trackballInterruptImpl1->intUpHandler();
        trackballInterruptImpl1->setIntervalFromNow(20);
    }
}
void TrackballInterruptImpl1::handleIntLeft()
{
    if (TB_DIRECTION == RISING || millis() > trackballInterruptImpl1->lastTime + 10) {
        trackballInterruptImpl1->lastTime = millis();
        trackballInterruptImpl1->intLeftHandler();
        trackballInterruptImpl1->setIntervalFromNow(20);
    }
}
void TrackballInterruptImpl1::handleIntRight()
{
    if (TB_DIRECTION == RISING || millis() > trackballInterruptImpl1->lastTime + 10) {
        trackballInterruptImpl1->lastTime = millis();
        trackballInterruptImpl1->intRightHandler();
        trackballInterruptImpl1->setIntervalFromNow(20);
    }
}
void TrackballInterruptImpl1::handleIntPressed()
{
    if (TB_DIRECTION == RISING || millis() > trackballInterruptImpl1->lastTime + 10) {
        trackballInterruptImpl1->lastTime = millis();
        trackballInterruptImpl1->intPressHandler();
        trackballInterruptImpl1->setIntervalFromNow(20);
    }
}
