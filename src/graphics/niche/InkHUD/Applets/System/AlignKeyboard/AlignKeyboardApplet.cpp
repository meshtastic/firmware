#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./AlignKeyboardApplet.h"

using namespace NicheGraphics;

InkHUD::AlignKeyboardApplet::AlignKeyboardApplet() : AlignStickApplet(false)
{
    title = "Align Keyboard:";
    instructions = "Press the arrow key in the direction indicated";
}

void InkHUD::AlignKeyboardApplet::onBackground()
{
    AlignStickApplet::onBackground();

    if (onClosed)
        onClosed();
}

// Unlike the joystick, settings->joystick.aligned is left alone: it belongs to the joystick

void InkHUD::AlignKeyboardApplet::onNavUp()
{
    if (!keyboardInput)
        return;

    sendToBackground();
}

void InkHUD::AlignKeyboardApplet::onNavDown()
{
    if (!keyboardInput)
        return;

    inkhud->rotateJoystick(2); // 180 deg
    sendToBackground();
}

void InkHUD::AlignKeyboardApplet::onNavLeft()
{
    if (!keyboardInput)
        return;

    inkhud->rotateJoystick(3); // 270 deg
    sendToBackground();
}

void InkHUD::AlignKeyboardApplet::onNavRight()
{
    if (!keyboardInput)
        return;

    inkhud->rotateJoystick(1); // 90 deg
    sendToBackground();
}

#endif
