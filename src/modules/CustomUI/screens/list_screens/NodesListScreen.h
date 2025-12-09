#pragma once

#include "BaseListScreen.h"
#include "../utils/LoRaHelper.h"
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
class NodesListScreen : public BaseListScreen {
public:
    NodesListScreen();
    virtual ~NodesListScreen();

    // BaseListScreen interface  
    virtual void onEnter() override;
    virtual void onExit() override;
    virtual bool handleKeyPress(char key) override;

protected:
    // BaseListScreen abstract methods
    virtual void drawItem(lgfx::LGFX_Device& tft, int index, int y, bool isSelected) override;
    virtual int getItemCount() override;
    virtual void onItemSelected(int index) override;
    virtual bool onBeforeDrawItems(lgfx::LGFX_Device& tft) override;

private:
    /**
     * Refresh the nodes list from mesh database
     */
    void refreshNodesList();
    
    /**
     * Draw signal strength bars from SNR
     */
    void drawSignalBars(lgfx::LGFX_Device& tft, int x, int y, int bars);
    
    /**
     * Format time since last heard for display
     */
    String formatTimeSince(uint32_t lastHeard);

    // Node data
    std::vector<NodeInfo> nodes;
    
    // UI state
    bool isLoading;         // Currently loading flag
    unsigned long lastRefreshTime;
    
    // Layout constants
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