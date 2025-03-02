#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Sub-menu for InkHUD::MenuApplet
Structure of the menu is defined in InkHUD::showPage

*/

#pragma once

#include "configuration.h"

namespace NicheGraphics::InkHUD
{

// Sub-menu for MenuApplet
enum MenuPage : uint8_t {
    ROOT, // Initial menu page
    SEND,
    OPTIONS,
    APPLETS,
    AUTOSHOW,
    RECENTS, // Select length of "recentlyActiveSeconds"
    EXIT,    // Dismiss the menu applet
};

} // namespace NicheGraphics::InkHUD

#endif