#pragma once

#include "../screens/BaseScreen.h"
#include "utils/LoRaHelper.h"
#include <vector>

/**
 * Nodes List Screen - Shows mesh nodes with selection
 * Features:
 * - Scrollable list of up to 15 nodes
 * - Signal strength indicators (SNR)
 * - Last heard time display
 * - Online/offline status
 * - Navigation: [A] Back, [1] Select
 */
class NodesListScreen : public BaseScreen {
public:
    NodesListScreen();
    virtual ~NodesListScreen();

    // BaseScreen interface
    virtual void onEnter() override;
    virtual void onExit() override;
    virtual void onDraw(lgfx::LGFX_Device& tft) override;
    virtual bool handleKeyPress(char key) override;

private:
    /**
     * Refresh the nodes list from mesh database
     */
    void refreshNodesList();
    
    /**
     * Draw the list of nodes in content area
     */
    void drawNodesList(lgfx::LGFX_Device& tft);
    
    /**
     * Draw individual node entry
     */
    void drawNodeEntry(lgfx::LGFX_Device& tft, int index, int y, bool isSelected);
    
    /**
     * Draw signal strength bars from SNR
     */
    void drawSignalBars(lgfx::LGFX_Device& tft, int x, int y, int bars);
    
    /**
     * Format time since last heard for display
     */
    String formatTimeSince(uint32_t lastHeard);
    
    /**
     * Handle scroll navigation
     */
    void scrollUp();
    void scrollDown();
    
    /**
     * Update selection and scrolling
     */
    void updateSelection();

    // Node data
    std::vector<NodeInfo> nodes;
    
    // UI state
    int selectedIndex;      // Currently selected node (0-based)
    int scrollOffset;       // First visible node index
    int maxVisibleItems;    // Max nodes visible at once
    bool isLoading;         // Currently loading flag
    unsigned long lastRefreshTime;
    
    // Layout constants
    static const int ITEM_HEIGHT = 20;
    static const int SIGNAL_BAR_WIDTH = 20;
    
    // Colors
    static const uint16_t COLOR_BLACK = 0x0000;
    static const uint16_t COLOR_GREEN = 0x07E0;
    static const uint16_t COLOR_YELLOW = 0xFFE0;
    static const uint16_t COLOR_DIM_GREEN = 0x4208;
    static const uint16_t COLOR_DARK_RED = 0x7800;
    static const uint16_t COLOR_BLUE = 0x001F;
    static const uint16_t COLOR_GRAY = 0x8410;
};