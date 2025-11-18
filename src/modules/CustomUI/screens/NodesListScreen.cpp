#include "NodesListScreen.h"
#include "gps/RTC.h" // for getTime() function
#include <Arduino.h>
#include <algorithm>

// Logging macro
#ifndef LOG_INFO
#define LOG_INFO(format, ...) Serial.printf("[INFO] " format "\n", ##__VA_ARGS__)
#endif

NodesListScreen::NodesListScreen() : BaseScreen("Mesh Nodes") {
    // Set navigation hints
    std::vector<NavHint> hints;
    hints.push_back(NavHint('A', "Back"));
    hints.push_back(NavHint('1', "Select"));
    setNavigationHints(hints);
    
    selectedIndex = 0;
    scrollOffset = 0;
    maxVisibleItems = 8; // 8 nodes visible in content area (160px / 20px per item)
    isLoading = false;
    lastRefreshTime = 0;
    
    LOG_INFO("📡 NodesListScreen: Created");
}

NodesListScreen::~NodesListScreen() {
    LOG_INFO("📡 NodesListScreen: Destroyed");
}

void NodesListScreen::onEnter() {
    LOG_INFO("📡 NodesListScreen: Entering screen");
    
    // Initialize state for fresh entry
    selectedIndex = 0;
    scrollOffset = 0;
    nodes.clear();
    isLoading = false;
    
    // Force immediate redraw to show screen first
    forceRedraw();
    
    // Load nodes on next update cycle
    lastRefreshTime = 0; // This will trigger refresh in onDraw
    
    LOG_INFO("📡 NodesListScreen: Screen ready, nodes will load on next update");
}

void NodesListScreen::onExit() {
    LOG_INFO("📡 NodesListScreen: Exiting screen");
}

void NodesListScreen::onDraw(lgfx::LGFX_Device& tft) {
    // Refresh nodes list periodically or on first load
    unsigned long now = millis();
    if (lastRefreshTime == 0 || (now - lastRefreshTime > 5000)) { // Refresh every 5 seconds
        refreshNodesList();
        lastRefreshTime = now;
    }
    
    // Clear content area
    tft.fillRect(0, getContentY(), getContentWidth(), getContentHeight(), COLOR_BLACK);
    
    if (isLoading) {
        // Show loading message
        tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
        tft.setTextSize(1);
        tft.setCursor(10, getContentY() + 20);
        tft.print("Loading mesh nodes...");
        return;
    }
    
    if (nodes.empty()) {
        // Show no nodes message
        tft.setTextColor(COLOR_DARK_RED, COLOR_BLACK);
        tft.setTextSize(1);
        tft.setCursor(10, getContentY() + 20);
        tft.print("No mesh nodes found");
        
        tft.setTextColor(COLOR_DIM_GREEN, COLOR_BLACK);
        tft.setCursor(10, getContentY() + 40);
        tft.print("Press [#] to refresh");
        return;
    }
    
    // Draw nodes list
    drawNodesList(tft);
}

bool NodesListScreen::handleKeyPress(char key) {
    LOG_INFO("📡 NodesListScreen: Key pressed: %c (isLoading: %s, nodes: %d)", 
        key, isLoading ? "true" : "false", nodes.size());
    
    if (isLoading) {
        return true; // Ignore keys while loading
    }
    
    switch (key) {
        case 'A':
        case 'a':
            LOG_INFO("📡 NodesListScreen: Back button pressed");
            // Will be handled by CustomUIModule for navigation back
            return false;
            
        case '1':
            if (!nodes.empty()) {
                LOG_INFO("📡 NodesListScreen: Select pressed for node: %s (0x%08x)", 
                    nodes[selectedIndex].longName.c_str(), nodes[selectedIndex].nodeNum);
                // TODO: Implement node selection/message
            }
            return true;
            
        case '2': // Up arrow
            LOG_INFO("📡 NodesListScreen: Scroll up - current: %d", selectedIndex);
            scrollUp();
            return true;
            
        case '8': // Down arrow  
            LOG_INFO("📡 NodesListScreen: Scroll down - current: %d", selectedIndex);
            scrollDown();
            return true;
            
        case '#':
            LOG_INFO("📡 NodesListScreen: Refreshing nodes list");
            refreshNodesList();
            return true;
            
        default:
            return false;
    }
}

void NodesListScreen::refreshNodesList() {
    LOG_INFO("📡 NodesListScreen: Refreshing nodes list");
    isLoading = true;
    
    // Force redraw to show loading state
    forceRedraw();
    
    // Get nodes from LoRa helper
    nodes = LoRaHelper::getNodesList(15, true);
    
    // Reset selection if current selection is out of bounds
    if (selectedIndex >= (int)nodes.size()) {
        selectedIndex = std::max(0, (int)nodes.size() - 1);
    }
    
    updateSelection();
    
    isLoading = false;
    LOG_INFO("📡 NodesListScreen: Refresh completed, found %d nodes", nodes.size());
    
    // Update display with results
    forceRedraw();
}

void NodesListScreen::drawNodesList(lgfx::LGFX_Device& tft) {
    int contentY = getContentY();
    int y = contentY + 5;
    
    // Calculate visible range
    int endIndex = std::min((int)nodes.size(), scrollOffset + maxVisibleItems);
    
    // Draw scroll indicators if needed
    if (scrollOffset > 0) {
        tft.setTextColor(COLOR_DIM_GREEN, COLOR_BLACK);
        tft.setCursor(300, contentY + 2);
        tft.print("^");
    }
    
    if (endIndex < nodes.size()) {
        tft.setTextColor(COLOR_DIM_GREEN, COLOR_BLACK);
        tft.setCursor(300, contentY + getContentHeight() - 10);
        tft.print("v");
    }
    
    // Draw visible nodes
    for (int i = scrollOffset; i < endIndex; i++) {
        bool isSelected = (i == selectedIndex);
        drawNodeEntry(tft, i, y, isSelected);
        y += ITEM_HEIGHT;
    }
}

void NodesListScreen::drawNodeEntry(lgfx::LGFX_Device& tft, int index, int y, bool isSelected) {
    const NodeInfo& node = nodes[index];
    
    // Selection highlight
    if (isSelected) {
        tft.fillRect(5, y - 2, getContentWidth() - 10, ITEM_HEIGHT - 2, COLOR_DIM_GREEN);
    }
    
    // Signal strength bars (first 20px)
    drawSignalBars(tft, 8, y + 2, node.signalBars);
    
    // Node long name (main area)
    uint16_t textColor = isSelected ? COLOR_YELLOW : COLOR_GREEN;
    if (!node.isOnline) {
        textColor = isSelected ? COLOR_GRAY : COLOR_DIM_GREEN;
    }
    
    tft.setTextColor(textColor, isSelected ? COLOR_DIM_GREEN : COLOR_BLACK);
    tft.setTextSize(1);
    
    String displayName = node.longName;
    if (displayName.length() > 18) {
        displayName = displayName.substring(0, 15) + "...";
    }
    
    tft.setCursor(35, y + 3);
    tft.print(displayName);
    
    // Last heard time (second line or right side)
    String timeStr = formatTimeSince(node.lastHeard);
    uint16_t timeColor = node.isOnline ? COLOR_GREEN : COLOR_GRAY;
    if (isSelected) timeColor = COLOR_YELLOW;
    
    tft.setTextColor(timeColor, isSelected ? COLOR_DIM_GREEN : COLOR_BLACK);
    tft.setCursor(35, y + 12);
    tft.setTextSize(1);
    tft.print(timeStr);
    
    // Status indicators (right side)
    int rightX = 250;
    
    // Favorite star
    if (node.isFavorite) {
        tft.setTextColor(COLOR_YELLOW, isSelected ? COLOR_DIM_GREEN : COLOR_BLACK);
        tft.setCursor(rightX, y + 6);
        tft.print("*");
        rightX += 10;
    }
    
    // Internet/MQTT indicator
    if (node.viaInternet) {
        tft.setTextColor(COLOR_BLUE, isSelected ? COLOR_DIM_GREEN : COLOR_BLACK);
        tft.setCursor(rightX, y + 6);
        tft.print("I");
        rightX += 10;
    }
    
    // Hops indicator
    if (node.hopsAway > 0) {
        tft.setTextColor(COLOR_DIM_GREEN, isSelected ? COLOR_DIM_GREEN : COLOR_BLACK);
        tft.setCursor(rightX, y + 6);
        tft.print(node.hopsAway);
    }
    
    // SNR value (small, bottom right)
    tft.setTextColor(COLOR_DIM_GREEN, isSelected ? COLOR_DIM_GREEN : COLOR_BLACK);
    tft.setCursor(270, y + 12);
    tft.print(node.snr, 1);
}

void NodesListScreen::drawSignalBars(lgfx::LGFX_Device& tft, int x, int y, int bars) {
    // Draw 4 possible bars, fill based on signal strength
    for (int i = 0; i < 4; i++) {
        int barHeight = 2 + (i * 2); // 2, 4, 6, 8 pixels high
        int barY = y + 12 - barHeight;
        int barX = x + (i * 3);
        
        uint16_t color = (i < bars) ? COLOR_GREEN : COLOR_DIM_GREEN;
        tft.fillRect(barX, barY, 2, barHeight, color);
    }
}

String NodesListScreen::formatTimeSince(uint32_t lastHeard) {
    if (lastHeard == 0) {
        return "Never";
    }
    
    uint32_t now = getTime();
    uint32_t elapsed = now - lastHeard;
    
    if (elapsed < 60) {
        return "Now";
    } else if (elapsed < 3600) { // Less than 1 hour
        int minutes = elapsed / 60;
        return String(minutes) + "m";
    } else if (elapsed < 86400) { // Less than 1 day
        int hours = elapsed / 3600;
        return String(hours) + "h";
    } else { // Days
        int days = elapsed / 86400;
        return String(days) + "d";
    }
}

void NodesListScreen::scrollUp() {
    LOG_INFO("📡 NodesListScreen: scrollUp called - selectedIndex: %d, nodes: %d", selectedIndex, nodes.size());
    if (selectedIndex > 0) {
        selectedIndex--;
        LOG_INFO("📡 NodesListScreen: scrollUp - new selectedIndex: %d", selectedIndex);
        updateSelection();
        forceRedraw();
    } else {
        LOG_INFO("📡 NodesListScreen: scrollUp - already at top");
    }
}

void NodesListScreen::scrollDown() {
    LOG_INFO("📡 NodesListScreen: scrollDown called - selectedIndex: %d, nodes: %d", selectedIndex, nodes.size());
    if (selectedIndex < (int)nodes.size() - 1) {
        selectedIndex++;
        LOG_INFO("📡 NodesListScreen: scrollDown - new selectedIndex: %d", selectedIndex);
        updateSelection();
        forceRedraw();
    } else {
        LOG_INFO("📡 NodesListScreen: scrollDown - already at bottom");
    }
}

void NodesListScreen::updateSelection() {
    // Adjust scroll offset to keep selection visible
    if (selectedIndex < scrollOffset) {
        scrollOffset = selectedIndex;
    } else if (selectedIndex >= scrollOffset + maxVisibleItems) {
        scrollOffset = selectedIndex - maxVisibleItems + 1;
    }
    
    // Ensure scroll offset is within bounds
    scrollOffset = std::max(0, std::min(scrollOffset, (int)nodes.size() - maxVisibleItems));
    if (scrollOffset < 0) scrollOffset = 0;
}