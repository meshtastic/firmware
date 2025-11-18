#pragma once

#include "../screens/BaseScreen.h"
#include "utils/WiFiHelper.h"
#include <vector>

/**
 * WiFi List Screen - Shows available WiFi networks with selection
 * Features:
 * - Scrollable list of up to 15 networks
 * - Signal strength indicators
 * - Security type display
 * - Navigation: [A] Back, [1] Select
 */
class WiFiListScreen : public BaseScreen {
public:
    WiFiListScreen();
    virtual ~WiFiListScreen();

    // BaseScreen interface
    virtual void onEnter() override;
    virtual void onExit() override;
    virtual void onDraw(lgfx::LGFX_Device& tft) override;
    virtual bool handleKeyPress(char key) override;

private:
    /**
     * Scan for WiFi networks and populate the list
     */
    void scanForNetworks();
    
    /**
     * Check if async scan is complete and process results
     */
    void checkScanComplete();
    
    /**
     * Draw the list of networks in content area
     */
    void drawNetworkList(lgfx::LGFX_Device& tft);
    
    /**
     * Draw individual network entry
     */
    void drawNetworkEntry(lgfx::LGFX_Device& tft, int index, int y, bool isSelected);
    
    /**
     * Draw signal strength bars
     */
    void drawSignalBars(lgfx::LGFX_Device& tft, int x, int y, int bars);
    
    /**
     * Handle scroll navigation
     */
    void scrollUp();
    void scrollDown();
    
    /**
     * Update selection and scrolling
     */
    void updateSelection();

    // Network data
    WiFiHelper wifiHelper;
    std::vector<WiFiNetworkInfo> networks;
    
    // UI state
    int selectedIndex;      // Currently selected network (0-based)
    int scrollOffset;       // First visible network index
    int maxVisibleItems;    // Max networks visible at once
    bool isScanning;        // Currently scanning flag
    unsigned long lastScanTime;
    
    // Layout constants
    static const int ITEM_HEIGHT = 20;
    static const int SIGNAL_BAR_WIDTH = 20;
    
    // Colors
    static const uint16_t COLOR_BLACK = 0x0000;
    static const uint16_t COLOR_GREEN = 0x07E0;
    static const uint16_t COLOR_YELLOW = 0xFFE0;
    static const uint16_t COLOR_DIM_GREEN = 0x4208;
    static const uint16_t COLOR_DARK_RED = 0x7800;
};