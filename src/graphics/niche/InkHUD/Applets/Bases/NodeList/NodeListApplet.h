#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Base class for Applets which display a list of nodes
Used by the "Recents" and "Heard" applets. Possibly more in future?

    +-------------------------------+
    |                            |  |
    |  SHRT                  . | |  |
    |  Long name              50km  |
    |                               |
    |  ABCD                 2 Hops  |
    |  abcdedfghijk           30km  |
    |                               |
    +-------------------------------+

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applet.h"

#include "main.h"

namespace NicheGraphics::InkHUD
{

class NodeListApplet : public Applet, public MeshModule
{
  protected:
    // Info needed to draw a node card to the list
    // - generated each time we hear a node
    struct CardInfo {
        static constexpr uint8_t HOPS_UNKNOWN = -1;
        static constexpr uint32_t DISTANCE_UNKNOWN = -1;

        NodeNum nodeNum = 0;
        SignalStrength signal = SignalStrength::SIGNAL_UNKNOWN;
        uint32_t distanceMeters = DISTANCE_UNKNOWN;
        uint8_t hopsAway = HOPS_UNKNOWN;
    };

  public:
    NodeListApplet(const char *name);

    void onRender() override;

    bool wantPacket(const meshtastic_MeshPacket *p) override;
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

  protected:
    virtual void handleParsed(CardInfo c) = 0; // Tell derived applet that we heard a node
    virtual std::string getHeaderText() = 0;   // Ask derived class what the applet's title should be

    uint8_t maxCards(); // Max number of cards which could ever fit on screen

    std::deque<CardInfo> cards; // Cards to be rendered. Derived applet fills this.

  private:
    void drawSignalIndicator(int16_t x, int16_t y, uint16_t w, uint16_t h,
                             SignalStrength signal); // Draw a "mobile phone" style signal indicator

    // Card Dimensions
    // - for rendering and for maxCards calc
    const uint8_t cardMarginH = fontSmall.lineHeight() / 2;                               // Gap between cards
    const uint16_t cardH = fontLarge.lineHeight() + fontSmall.lineHeight() + cardMarginH; // Height of card
};

} // namespace NicheGraphics::InkHUD

#endif