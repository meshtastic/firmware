#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

T5 Nodes: every known node, most recently heard first, in pages above the T5 nav.
One applet, two compositions picked from the live dimensions: Carry (two-line rows) and Console (table).

*/

#pragma once

#include "configuration.h"

#include "./T5Applet.h"

#include "mesh/MeshModule.h"

namespace NicheGraphics::InkHUD
{

class T5NodesApplet : public T5Applet, public MeshModule
{
  public:
    T5NodesApplet() : MeshModule("T5NodesApplet") { hideFromAppSwitcher = true; } // Reached from the NODES nav cell

    void onActivate() override;
    void onDeactivate() override;
    void onRender(bool full) override;
    bool onTouchPoint(uint16_t x, uint16_t y, bool longPress) override;
    void onNavUp() override;
    void onNavDown() override;

    bool wantPacket(const meshtastic_MeshPacket *p) override;
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    // One node, as Nodes and Node detail show it. Strings are already parse()d, and empty when unknown
    struct Row {
        std::string shortName, longName, distance, bearing, snr;
        std::string rssi;       // Only with signal: a direct reception this session
        int32_t heardSecs = -1; // -1: never heard with a valid clock
        int16_t hops = -1;      // -1: unknown
        int16_t degrees = -1;   // Bearing from us; -1: no valid positions
        SignalStrength signal = SIGNAL_UNKNOWN;
    };

    Row readRow(meshtastic_NodeInfoLite *node);

  private:
    struct Reception {
        NodeNum from;
        SignalStrength signal;
        int32_t rssi;
    };

    uint8_t rowsPerPage() { return isConsole() ? 4 : 9; }
    int16_t rowsTop() { return isConsole() ? 98 : 80; }
    uint8_t rowHeight() { return isConsole() ? 78 : 84; }

    std::vector<meshtastic_NodeInfoLite *> sortedNodes();
    void renderCarry(const std::vector<Row> &rows, uint16_t nodes, uint16_t heard);
    void renderConsole(const std::vector<Row> &rows, uint16_t nodes, uint16_t heard, uint16_t direct);
    void drawSignal(int16_t left, int16_t bottom, SignalStrength signal);

    std::vector<Reception> signals; // Last direct radio reception of each node, this session
    std::vector<NodeNum> shown;     // Nodes on screen, top to bottom: what a row tap opens
    uint16_t firstRow = 0;          // List index of the top row on screen
};

} // namespace NicheGraphics::InkHUD

#endif
