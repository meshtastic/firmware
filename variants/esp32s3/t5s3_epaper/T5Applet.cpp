#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./T5Applet.h"

#include "graphics/niche/InkHUD/SystemApplet.h"
#include "mesh/NodeDB.h"

#include <initializer_list>

using namespace NicheGraphics;

int8_t InkHUD::T5Applet::indexOf(const char *name)
{
    const std::vector<Applet *> &applets = InkHUD::getInstance()->userApplets;
    for (uint8_t i = 0; i < applets.size(); i++) {
        if (strcmp(applets[i]->name, name) == 0)
            return i;
    }
    return -1;
}

// The menu can re-enable the shared icon, which would overlap T5 headers: switched back off whenever a T5 screen shows
void InkHUD::T5Applet::onForeground()
{
    settings->optionalFeatures.batteryIcon = false;
    inkhud->getSystemApplet("BatteryIcon")->sendToBackground();
}

// Heard within the last 10 minutes: the HEARD 10M count, on every T5 screen
bool InkHUD::T5Applet::heardRecently(const meshtastic_NodeInfoLite *node)
{
    return node->last_heard && sinceLastSeen(node) < 10 * 60;
}

std::string InkHUD::T5Applet::hopsString(uint8_t hops)
{
    return hops == 0 ? "direct" : to_string(hops) + (hops == 1 ? " hop" : " hops");
}

std::string InkHUD::T5Applet::sinceString(uint32_t secs)
{
    if (secs < 60)
        return "now";
    if (secs < 60 * 60)
        return to_string(secs / 60) + " min";
    if (secs < 24 * 60 * 60)
        return to_string(secs / (60 * 60)) + " h";
    return to_string(secs / (24 * 60 * 60)) + " d";
}

std::string InkHUD::T5Applet::agoString(uint32_t secs)
{
    return secs < 60 ? sinceString(secs) : sinceString(secs) + " ago";
}

// Non-empty parts only, separated by a Win-1253 middle dot
std::string InkHUD::T5Applet::join(const std::string &a, const std::string &b)
{
    return a.empty() ? b : b.empty() ? a : a + " \xB7 " + b;
}

// A destination opens the first active applet of its kind, in registration order
int8_t InkHUD::T5Applet::destinationApplet(Destination d)
{
    auto firstActive = [this](std::initializer_list<const char *> names) -> int8_t {
        for (const char *name : names) {
            const int8_t i = indexOf(name);
            if (i >= 0 && inkhud->userApplets[i]->isActive())
                return i;
        }
        return -1;
    };

    switch (d) {
    case HOME:
        return firstActive({"Home"});
    case MSGS:
        return firstActive({"All Messages", "DMs", "Channel 0", "Channel 1"});
    case NODES:
        return firstActive({"Nodes"});
    case MAP:
        return firstActive({"Positions", "Favorites Map"});
    default: // MENU and APPS open system applets
        return -1;
    }
}

// Six cells across navWidth(): the current one inverted, unavailable ones struck through
void InkHUD::T5Applet::drawNav(Destination current)
{
    static constexpr const char *labels[] = {"HOME", "MSGS", "NODES", "MAP", "MENU", "APPS"};
    const int16_t top = navTop();
    const uint16_t navW = navWidth();

    drawLine(0, top - 1, width() - 1, top - 1, BLACK);
    setFont(fontSmall);
    for (uint8_t i = 0; i < 6; i++) {
        const int16_t left = i * navW / 6;
        const int16_t cellW = (i + 1) * navW / 6 - left;
        const int16_t centerX = left + cellW / 2;
        const int16_t centerY = top + NAV_H / 2;

        if (i == current) {
            fillRect(left, top, cellW, NAV_H, BLACK);
            setTextColor(WHITE);
            printThick(centerX, centerY, labels[i], 2, 1);
            setTextColor(BLACK);
            continue;
        }

        if (i > 0)
            drawLine(left, top, left, height() - 1, BLACK);
        printAt(centerX, centerY, labels[i], CENTER, MIDDLE);

        const bool available = i == MENU || i == APPS || destinationApplet((Destination)i) >= 0;
        if (!available)
            fillRect(left + 14, centerY - 1, cellW - 28, 2, BLACK);
    }

    if (isConsole())
        fillRect(navW, top, 2, NAV_H, BLACK); // Rule between the six cells and the mode slot
}

// Hit-tests the six cells only: the Console mode slot never resolves to a cell
bool InkHUD::T5Applet::handleNavTap(uint16_t x, uint16_t y)
{
    if (y < navTop() || x >= navWidth())
        return false;

    const Destination d = (Destination)(x * 6 / navWidth());
    switch (d) {
    case MENU:
        inkhud->openMenu();
        break;
    case APPS:
        inkhud->openAppSwitcher();
        break;
    default: {
        // The screen already shown stays put, without a refresh
        const int8_t target = destinationApplet(d);
        if (target >= 0 && !inkhud->userApplets[target]->isForeground())
            inkhud->showApplet(target);
        break;
    }
    }
    return true;
}

void InkHUD::T5Applet::printBold(int16_t x, int16_t y, const std::string &text, HorizontalAlignment ha)
{
    const int16_t w = getTextWidth(text);
    const int16_t centerX = ha == LEFT ? x + w / 2 : ha == RIGHT ? x - w / 2 : x;
    printThick(centerX, y + getFont().lineHeight() / 2, text, 2, 1);
}

#endif
