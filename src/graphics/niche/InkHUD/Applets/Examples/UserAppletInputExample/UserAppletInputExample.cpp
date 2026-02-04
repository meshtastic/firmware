#ifdef MESHTASTIC_INCLUDE_INKHUD
#include "./UserAppletInputExample.h"

using namespace NicheGraphics;

void InkHUD::UserAppletInputExampleApplet::onActivate()
{
    setGrabbed(false);
}

void InkHUD::UserAppletInputExampleApplet::onRender(bool full)
{
    drawHeader("Input Example");
    uint16_t headerHeight = getHeaderHeight();

    std::string buttonName;
    if (settings->joystick.enabled)
        buttonName = "joystick center button";
    else
        buttonName = "user button";

    std::string additional = " | Control is grabbed, long press " + buttonName + " to release controls";
    if (!isGrabbed)
        additional = " | Control is released, long press " + buttonName + " to grab controls";

    printWrapped(0, headerHeight, width(), "Last button: " + lastInput + additional);
}

void InkHUD::UserAppletInputExampleApplet::setGrabbed(bool grabbed)
{
    isGrabbed = grabbed;
    setInput(BUTTON_SHORT | EXIT_SHORT | EXIT_LONG | NAV_UP | NAV_DOWN | NAV_LEFT | NAV_RIGHT,
             grabbed);           // Enables/disables grabbing all inputs
    setInput(BUTTON_LONG, true); // Always grab this input
}

void InkHUD::UserAppletInputExampleApplet::onButtonShortPress()
{
    lastInput = "BUTTON_SHORT";
    requestUpdate();
}
void InkHUD::UserAppletInputExampleApplet::onButtonLongPress()
{
    lastInput = "BUTTON_LONG";
    setGrabbed(!isGrabbed);
    requestUpdate();
}
void InkHUD::UserAppletInputExampleApplet::onExitShort()
{
    lastInput = "EXIT_SHORT";
    requestUpdate();
}
void InkHUD::UserAppletInputExampleApplet::onExitLong()
{
    lastInput = "EXIT_LONG";
    requestUpdate();
}
void InkHUD::UserAppletInputExampleApplet::onNavUp()
{
    lastInput = "NAV_UP";
    requestUpdate();
}
void InkHUD::UserAppletInputExampleApplet::onNavDown()
{
    lastInput = "NAV_DOWN";
    requestUpdate();
}
void InkHUD::UserAppletInputExampleApplet::onNavLeft()
{
    lastInput = "NAV_LEFT";
    requestUpdate();
}
void InkHUD::UserAppletInputExampleApplet::onNavRight()
{
    lastInput = "NAV_RIGHT";
    requestUpdate();
}

#endif