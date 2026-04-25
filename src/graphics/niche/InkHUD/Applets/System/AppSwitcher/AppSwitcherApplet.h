#ifdef MESHTASTIC_INCLUDE_INKHUD

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/SystemApplet.h"

#include <vector>

namespace NicheGraphics::InkHUD
{

class Tile;

class AppSwitcherApplet : public SystemApplet
{
  public:
    AppSwitcherApplet();

    void onForeground() override;
    void onBackground() override;
    void onRender(bool full) override;

    void onButtonShortPress() override;
    void onButtonLongPress() override;
    void onExitShort() override;
    void onNavUp() override;
    void onNavDown() override;
    bool onTouchPoint(uint16_t x, uint16_t y, bool longPress) override;

    // Open the app switcher on a user tile and temporarily replace the tile's owner.
    void show(Tile *t);

  private:
    void rebuildActiveAppletList();
    void clampSelection();
    uint8_t cardsPerPage() const;
    uint8_t currentPage() const;
    void stepPage(int8_t delta);
    void activateSelectedApplet();

    std::vector<uint8_t> activeAppletIndices;
    uint8_t selectedIndex = 0;

    Applet *borrowedTileOwner = nullptr;
};

} // namespace NicheGraphics::InkHUD

#endif
