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
    T5NodesApplet() : MeshModule("T5NodesApplet") {}

    void onActivate() override;
    void onDeactivate() override;
    void onForeground() override;
    void onRender(bool full) override;
    bool onTouchPoint(uint16_t x, uint16_t y, bool longPress) override;
    void onNavUp() override;
    void onNavDown() override;

    bool wantPacket(const meshtastic_MeshPacket *p) override;
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

  private:
    // One on-screen node. Strings are already parse()d, and empty when unknown
    struct Row {
        std::string shortName, longName, distance, bearing, snr;
        int32_t heardSecs = -1; // -1: never heard with a valid clock
        int16_t hops = -1;      // -1: unknown
        SignalStrength signal = SIGNAL_UNKNOWN;
    };

    uint8_t rowsPerPage() { return isConsole() ? 4 : 9; }
    int16_t rowsTop() { return isConsole() ? 98 : 80; }
    uint8_t rowHeight() { return isConsole() ? 78 : 84; }

    std::vector<meshtastic_NodeInfoLite *> sortedNodes();
    Row readRow(meshtastic_NodeInfoLite *node);
    void renderCarry(const std::vector<Row> &rows, uint16_t nodes, uint16_t heard);
    void renderConsole(const std::vector<Row> &rows, uint16_t nodes, uint16_t heard, uint16_t direct);
    void drawSignal(int16_t left, int16_t bottom, SignalStrength signal);

    std::vector<std::pair<NodeNum, SignalStrength>> signals; // Last direct radio reception of each node, this session
    std::vector<NodeNum> shown;                              // Nodes on screen, top to bottom: what a row tap selects
    uint16_t firstRow = 0;                                   // List index of the top row on screen
    NodeNum selected = 0;                                    // Last tapped node, for Node detail
};

} // namespace NicheGraphics::InkHUD

#endif
