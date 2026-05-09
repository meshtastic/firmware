#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

One item of a MenuPage, in InkHUD::MenuApplet

Added to MenuPages in InkHUD::showPage

- May open a submenu or exit
- May perform an action
- May toggle a bool value, shown by a checkbox

*/

#pragma once

#include "configuration.h"

#include "./MenuAction.h"
#include "./MenuPage.h"

namespace NicheGraphics::InkHUD
{

// One item of a MenuPage
class MenuItem
{
  public:
    std::string label;
    MenuAction action = NO_ACTION;
    MenuPage nextPage = EXIT;
    bool *checkState = nullptr;
    bool isHeader = false; // Non-selectable section label

    // Various constructors, depending on the intended function of the item

    MenuItem(const char *label, MenuPage nextPage) : label(label), nextPage(nextPage) {}
    MenuItem(const char *label, MenuAction action) : label(label), action(action) {}
    MenuItem(const char *label, MenuAction action, MenuPage nextPage) : label(label), action(action), nextPage(nextPage) {}
    MenuItem(const char *label, MenuAction action, MenuPage nextPage, bool *checkState)
        : label(label), action(action), nextPage(nextPage), checkState(checkState)
    {
    }
    static MenuItem Header(const char *label)
    {
        MenuItem item(label, NO_ACTION, EXIT);
        item.isHeader = true;
        return item;
    }
};

} // namespace NicheGraphics::InkHUD

#endif