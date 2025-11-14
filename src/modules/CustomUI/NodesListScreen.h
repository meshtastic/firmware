#pragma once

#include "BaseScreen.h"
#include "UIDataState.h"
#include "mesh/NodeDB.h"

/**
 * Screen showing list of mesh nodes with efficient updates
 * Only redraws when node list actually changes
 */
class NodesListScreen : public BaseScreen {
public:
    NodesListScreen(UINavigator* navigator);
    
    // BaseScreen interface
    void onEnter() override;
    void onExit() override;
    void handleInput(uint8_t input) override;
    void draw(Adafruit_ST7789& tft, UIDataState& dataState) override;
    bool needsUpdate(UIDataState& dataState) override;

private:
    int selectedIndex;
    int scrollOffset;
    static const int NODES_PER_PAGE = 8; // Number of nodes visible on screen
    
    void drawNodeList(Adafruit_ST7789& tft, const UIDataState::NodesData& data, bool forceRedraw = false);
    void drawNodeEntry(Adafruit_ST7789& tft, const char* nodeName, uint32_t nodeId, 
                      uint32_t lastHeard, int index, int y, bool selected);
    void adjustScrollOffset(size_t nodeCount);
    const char* getNodeStatusText(uint32_t lastHeard, uint16_t& color);
};