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

namespace NicheGraphics::InkHUD
{

class NodeListApplet : public Applet, public MeshModule
{
  protected:
    // Info used to draw one card to the node list
    struct CardInfo {
        static constexpr uint8_t HOPS_UNKNOWN = -1;
        static constexpr uint32_t DISTANCE_UNKNOWN = -1;

        NodeNum nodeNum = 0;
        SignalStrength signal = SignalStrength::SIGNAL_UNKNOWN;
        uint32_t distanceMeters = DISTANCE_UNKNOWN;
        uint8_t hopsAway = HOPS_UNKNOWN; // Unknown
    };

  public:
    NodeListApplet(const char *name);
    void onRender() override;

    // MeshModule overrides
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

  protected:
    virtual void handleParsed(CardInfo c) = 0; // Pass extracted info from a new packet to derived class, for sorting and storage
    virtual std::string getHeaderText() = 0;   // Title for the applet's header. Todo: get this info another way?

    uint8_t maxCards(); // Calculate the maximum number of cards an applet could ever display

    std::deque<CardInfo> cards; // Derived applet places cards here, for this base applet to render

  private:
    // UI element: a "mobile phone" style signal indicator
    void drawSignalIndicator(int16_t x, int16_t y, uint16_t w, uint16_t h, SignalStrength signal);

    // Dimensions for drawing
    // Used for render, and also for maxCards calc
    const uint8_t cardMarginH = fontSmall.lineHeight() / 2;                               // Gap between cards
    const uint16_t cardH = fontLarge.lineHeight() + fontSmall.lineHeight() + cardMarginH; // Height of card
};

} // namespace NicheGraphics::InkHUD

#endif