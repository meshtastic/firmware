#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./AlignKeyboardApplet.h"

using namespace NicheGraphics;

InkHUD::AlignKeyboardApplet::AlignKeyboardApplet() : AlignStickApplet(false)
{
    title = "Align Keyboard:";
    instructions = "Press the arrow key in the direction indicated";
}

// Unlike the joystick, settings->joystick.aligned is left alone: it belongs to the joystick

void InkHUD::AlignKeyboardApplet::onNavUp()
{
    sendToBackground();
}

void InkHUD::AlignKeyboardApplet::onNavDown()
{
    inkhud->rotateJoystick(2); // 180 deg
    sendToBackground();
}

void InkHUD::AlignKeyboardApplet::onNavLeft()
{
    inkhud->rotateJoystick(3); // 270 deg
    sendToBackground();
}

void InkHUD::AlignKeyboardApplet::onNavRight()
{
    inkhud->rotateJoystick(1); // 90 deg
    sendToBackground();
}

#endif
