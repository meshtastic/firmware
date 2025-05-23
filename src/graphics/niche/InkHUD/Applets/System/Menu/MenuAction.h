#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Set of end-point actions for the Menu Applet

Added as menu entries in MenuApplet::showPage
Behaviors assigned in MenuApplet::execute

*/

#pragma once

#include "configuration.h"

namespace NicheGraphics::InkHUD
{

enum MenuAction {
    NO_ACTION,
    SEND_PING,
    SHUTDOWN,
    NEXT_TILE,
    TOGGLE_BACKLIGHT,
    TOGGLE_GPS,
    ENABLE_BLUETOOTH,
    TOGGLE_APPLET,
    TOGGLE_AUTOSHOW_APPLET,
    SET_RECENTS,
    ROTATE,
    LAYOUT,
    TOGGLE_BATTERY_ICON,
    TOGGLE_NOTIFICATIONS,
    TOGGLE_12H_CLOCK,
};

} // namespace NicheGraphics::InkHUD

#endif