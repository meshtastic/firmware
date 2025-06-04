#include "TrackballInterruptImpl1.h"
#include "InputBroker.h"
#include "configuration.h"

#include "main.h"
#include "modules/CannedMessageModule.h"
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
    uint8_t pinmenu = BUTTON_PIN;

    char eventDown = static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_DOWN);
    char eventUp = static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_UP);
    char eventLeft = static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_LEFT);
    char eventRight = static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_RIGHT);
    char eventPressed = static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT);
    char eventcancel =
        static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_CANCEL);

    TrackballInterruptBase::init(pinDown, pinUp, pinLeft, pinRight, pinPress, pinmenu,
                                 eventDown, eventUp, eventLeft, eventRight, eventPressed,
                                 eventcancel,
                                 TrackballInterruptImpl1::handleIntDown, TrackballInterruptImpl1::handleIntUp,
                                 TrackballInterruptImpl1::handleIntLeft, TrackballInterruptImpl1::handleIntRight,
                                 TrackballInterruptImpl1::handleIntPressed, TrackballInterruptImpl1::handleMenuPressed);
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

void TrackballInterruptImpl1::handleMenuPressed()
{
    bool activateMenuAction = false;

    
    if (cannedMessageModule && cannedMessageModule->isUIVisibleAndInterceptingInput()) {
        activateMenuAction = true;
    }

    if (activateMenuAction) {
        
        if (trackballInterruptImpl1) {
            LOG_DEBUG("Menu pressed; Canned Message UI is active and intercepting input. Activating CANCEL.");
            trackballInterruptImpl1->intMenuHandler();
        }
    } else {
        LOG_DEBUG("Menu pressed, but Canned Message module is not intercepting input. No action from trackball.");
    }
}