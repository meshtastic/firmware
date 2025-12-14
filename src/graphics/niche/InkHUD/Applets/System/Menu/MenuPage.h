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
    CANNEDMESSAGE_RECIPIENT, // Select destination for a canned message
    OPTIONS,
    NODE_CONFIG,
    NODE_CONFIG_LORA,
    NODE_CONFIG_ROLE,
    APPLETS,
    AUTOSHOW,
    RECENTS, // Select length of "recentlyActiveSeconds"
    REGION,
    EXIT, // Dismiss the menu applet
};

} // namespace NicheGraphics::InkHUD

#endif