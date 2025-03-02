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
    SEND_NODEINFO,
    SEND_POSITION,
    SHUTDOWN,
    NEXT_TILE,
    TOGGLE_APPLET,
    ACTIVATE_APPLETS, // Todo: remove? Possible redundant, handled by TOGGLE_APPLET?
    TOGGLE_AUTOSHOW_APPLET,
    SET_RECENTS,
    ROTATE,
    LAYOUT,
    TOGGLE_BATTERY_ICON,
    TOGGLE_NOTIFICATIONS,
    TOGGLE_BACKLIGHT,
};

} // namespace NicheGraphics::InkHUD

#endif