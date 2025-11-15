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
    // New row-based rendering methods
    void drawNodesContent(Adafruit_ST7789& tft, UIDataState& dataState);
    void drawNodeRow(Adafruit_ST7789& tft, const UIDataState::NodesData& nodesData, int nodeIndex, int rowIndex);
};