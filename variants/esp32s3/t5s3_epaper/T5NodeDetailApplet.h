#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

T5 Node detail: one node's facts, its position relative to ours, and COMPOSE MESSAGE / OPEN MAP, above the T5 nav.
Opened from a T5 Nodes row. One applet, two compositions picked from the live dimensions: Carry and Console.

*/

#pragma once

#include "configuration.h"

#include "./T5Applet.h"

namespace NicheGraphics::InkHUD
{

class T5NodeDetailApplet : public T5Applet
{
  public:
    static bool open(NodeNum num); // Show a nodeDB node. False, and nothing shown, if that isn't possible

    void onRender(bool full) override;
    bool onTouchPoint(uint16_t x, uint16_t y, bool longPress) override;

  private:
    NodeNum node = 0; // Shown node, re-read from nodeDB on every render
};

} // namespace NicheGraphics::InkHUD

#endif
