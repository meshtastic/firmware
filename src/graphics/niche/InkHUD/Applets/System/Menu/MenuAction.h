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
    ROTATE,
    LAYOUT,
};

} // namespace NicheGraphics::InkHUD

#endif