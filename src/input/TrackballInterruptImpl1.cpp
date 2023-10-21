#include "TrackballInterruptImpl1.h"
#include "InputBroker.h"
#include "configuration.h"

TrackballInterruptImpl1 *trackballInterruptImpl1;

TrackballInterruptImpl1::TrackballInterruptImpl1() : TrackballInterruptBase("trackball1") {}

void TrackballInterruptImpl1::init()
{
#if !HAS_TRACKBALL
    // Input device is disabled.
    return;
#else
    uint8_t pinUp = TB_UP;
    uint8_t pinDown = TB_DOWN;
    uint8_t pinLeft = TB_LEFT;
    uint8_t pinRight = TB_RIGHT;
    uint8_t pinPress = TB_PRESS;

    char eventDown = static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_DOWN);
    char eventUp = static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_UP);
    char eventLeft = static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_LEFT);
    char eventRight = static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_RIGHT);
    char eventPressed = static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT);

    TrackballInterruptBase::init(pinDown, pinUp, pinLeft, pinRight, pinPress, eventDown, eventUp, eventLeft, eventRight,
                                 eventPressed, TrackballInterruptImpl1::handleIntDown, TrackballInterruptImpl1::handleIntUp,
                                 TrackballInterruptImpl1::handleIntLeft, TrackballInterruptImpl1::handleIntRight,
                                 TrackballInterruptImpl1::handleIntPressed);
    inputBroker->registerSource(this);
#endif
}

void TrackballInterruptImpl1::handleIntDown()
{
    trackballInterruptImpl1->intDownHandler();
}
void TrackballInterruptImpl1::handleIntUp()
{
    trackballInterruptImpl1->intUpHandler();
}
void TrackballInterruptImpl1::handleIntLeft()
{
    trackballInterruptImpl1->intLeftHandler();
}
void TrackballInterruptImpl1::handleIntRight()
{
    trackballInterruptImpl1->intRightHandler();
}
void TrackballInterruptImpl1::handleIntPressed()
{
    trackballInterruptImpl1->intPressHandler();
}
