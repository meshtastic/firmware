#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Base for the T5-only InkHUD applets.
Draws and hit-tests the frozen six-destination nav bar: HOME · MSGS · NODES · MAP · MENU · APPS

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applet.h"

namespace NicheGraphics::InkHUD
{

class T5Applet : public Applet
{
  public:
    static constexpr uint16_t NAV_H = 88;
    static constexpr uint16_t CONSOLE_SLOT_W = 150; // Console: trailing mode slot, outside the six nav cells

    static int8_t indexOf(const char *name); // Index in InkHUD::userApplets by registered name, or -1

    void onForeground() override; // Hides the shared battery icon: every T5 screen draws its own battery status

  protected:
    enum Destination : uint8_t { HOME, MSGS, NODES, MAP, MENU, APPS };

    bool isConsole() { return width() > height(); }
    uint16_t navWidth() { return isConsole() ? width() - CONSOLE_SLOT_W : width(); }
    int16_t navTop() { return height() - NAV_H; }

    void drawNav(Destination current);
    bool handleNavTap(uint16_t x, uint16_t y); // True if the tap hit one of the six cells
    void printBold(int16_t x, int16_t y, const std::string &text, HorizontalAlignment ha = LEFT); // Top-aligned

    static bool heardRecently(const meshtastic_NodeInfoLite *node);
    static std::string hopsString(uint8_t hops);   // "direct", "1 hop", "3 hops"
    static std::string sinceString(uint32_t secs); // "now", "5 min", "3 h", "2 d"
    static std::string agoString(uint32_t secs);   // "now", "5 min ago", ...
    static std::string join(const std::string &a, const std::string &b);

    int8_t destinationApplet(Destination d); // Applet index a destination opens, or -1 if unavailable
};

} // namespace NicheGraphics::InkHUD

#endif
