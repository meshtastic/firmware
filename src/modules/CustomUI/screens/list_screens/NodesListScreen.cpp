#include "NodesListScreen.h"
#include "gps/RTC.h" // for getTime() function
#include <Arduino.h>
#include <algorithm>

// Logging macro
#ifndef LOG_INFO
#define LOG_INFO(format, ...) Serial.printf("[INFO] " format "\n", ##__VA_ARGS__)
#endif

NodesListScreen::NodesListScreen() : BaseListScreen("Mesh Nodes", 20) {
    // Set navigation hints
    std::vector<NavHint> hints;
    hints.push_back(NavHint('A', "Back"));
    hints.push_back(NavHint('1', "Select"));
    setNavigationHints(hints);
    
    isLoading = false;
    lastRefreshTime = 0;
    
    LOG_INFO("📡 NodesListScreen: Created");
}

NodesListScreen::~NodesListScreen() {
    LOG_INFO("📡 NodesListScreen: Destroyed");
}

void NodesListScreen::onEnter() {
    LOG_INFO("📡 NodesListScreen: Entering screen");
    
    // Call parent onEnter
    BaseListScreen::onEnter();
    
    // Initialize nodes state
    nodes.clear();
    isLoading = false;
    
    // Load nodes on next update cycle
    lastRefreshTime = 0; // This will trigger refresh in onBeforeDrawItems
    
    LOG_INFO("📡 NodesListScreen: Screen ready, nodes will load on next update");
}

void NodesListScreen::onExit() {
    LOG_INFO("📡 NodesListScreen: Exiting screen - cleaning memory");
    
    // Call parent onExit
    BaseListScreen::onExit();
    
    // Force complete vector deallocation
    nodes.clear();
    nodes.shrink_to_fit();
    std::vector<NodeInfo>().swap(nodes);
    
    // Reset nodes state
    isLoading = false;
    lastRefreshTime = 0;
    
    // Log memory cleanup
    LOG_INFO("📡 NodesListScreen: Vector memory deallocated, state reset");
}

void NodesListScreen::onBeforeDrawItems(lgfx::LGFX_Device& tft) {
    // Refresh nodes list periodically or on first load
    unsigned long now = millis();
    if (lastRefreshTime == 0 || (now - lastRefreshTime > 10000)) { // Refresh every 10 seconds (reduced frequency)
        refreshNodesList();
        lastRefreshTime = now;
    }
    
    if (isLoading) {
        // BaseListScreen will handle clearing - just show loading message
        tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
        tft.setTextSize(1);
        tft.setCursor(10, getContentY() + 20);
        tft.print("Loading mesh nodes...");
        return;
    }
    
    if (nodes.empty()) {
        // BaseListScreen will handle clearing - just show no nodes message
        tft.setTextColor(COLOR_DARK_RED, COLOR_BLACK);
        tft.setTextSize(1);
        tft.setCursor(10, getContentY() + 20);
        tft.print("No mesh nodes found");
        
        tft.setTextColor(COLOR_DIM_GREEN, COLOR_BLACK);
        tft.setCursor(10, getContentY() + 40);
        tft.print("Press [#] to refresh");
        return;
    }
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
            
        case '#':
            LOG_INFO("📡 NodesListScreen: Refreshing nodes list");
            refreshNodesList();
            return true;
            
        default:
            // Let BaseListScreen handle navigation (arrow keys, select)
            return BaseListScreen::handleKeyPress(key);
    }
}

void NodesListScreen::refreshNodesList() {
    LOG_INFO("📡 NodesListScreen: Refreshing nodes list");
    isLoading = true;
    
    // Get nodes from LoRa helper
    std::vector<NodeInfo> newNodes = LoRaHelper::getNodesList(15, true);
    
    // Only update if data actually changed
    bool dataChanged = (newNodes.size() != nodes.size());
    if (!dataChanged) {
        // Check if any node data changed
        for (size_t i = 0; i < newNodes.size() && i < nodes.size(); i++) {
            if (newNodes[i].nodeNum != nodes[i].nodeNum || 
                newNodes[i].lastHeard != nodes[i].lastHeard ||
                newNodes[i].snr != nodes[i].snr) {
                dataChanged = true;
                break;
            }
        }
    }
    
    if (dataChanged) {
        nodes = newNodes;
        
        // Reset selection if current selection is out of bounds
        if (getSelectedIndex() >= static_cast<int>(nodes.size())) {
            setSelection(std::max(0, static_cast<int>(nodes.size()) - 1));
        }
        
        // Only invalidate list if data actually changed
        invalidateList();
        LOG_INFO("📡 NodesListScreen: Data changed, list invalidated");
    }
    
    isLoading = false;
    LOG_INFO("📡 NodesListScreen: Refresh completed, found %d nodes (changed: %s)", nodes.size(), dataChanged ? "yes" : "no");
}

void NodesListScreen::onItemSelected(int index) {
    if (index >= 0 && index < static_cast<int>(nodes.size())) {
        LOG_INFO("📡 NodesListScreen: Selected node: %s (0x%08x)", 
            nodes[index].longName, nodes[index].nodeNum);
        // TODO: Implement node selection/message functionality
    }
}

int NodesListScreen::getItemCount() {
    return static_cast<int>(nodes.size());
}

void NodesListScreen::drawSignalBars(lgfx::LGFX_Device& tft, int x, int y, int bars) {
    // Draw 4 possible bars, fill based on signal strength
    for (int i = 0; i < 4; i++) {
        int barHeight = 2 + (i * 2); // 2, 4, 6, 8 pixels high
        int barY = y + 12 - barHeight;
        int barX = x + (i * 3);
        
        uint16_t color = (i < bars) ? COLOR_GREEN : 0x2104; // Bright green or dark green
        tft.fillRect(barX, barY, 2, barHeight, color);
    }
}

void NodesListScreen::drawItem(lgfx::LGFX_Device& tft, int index, int y, bool isSelected) {
    if (index < 0 || index >= static_cast<int>(nodes.size())) {
        return; // Invalid index
    }
    
    const NodeInfo& node = nodes[index];
    
    // BaseListScreen needs COLOR_SELECTION constant
    static const uint16_t COLOR_SELECTION = 0x4208; // Dim green for selection
    
    // Signal strength bars (first 20px)
    drawSignalBars(tft, 8, y + 2, node.signalBars);
    
    // Node long name (main area) - use char array directly
    uint16_t textColor = isSelected ? 0xFFFF : COLOR_GREEN; // White when selected, green when not
    if (!node.isOnline) {
        textColor = isSelected ? 0xC618 : COLOR_DIM_GREEN; // Light grey when selected, dim green when not
    }
    
    uint16_t bgColor = isSelected ? COLOR_SELECTION : COLOR_BLACK;
    tft.setTextColor(textColor, bgColor);
    tft.setTextSize(1);
    
    // Create display name with truncation
    char displayName[19]; // 18 chars + null
    strncpy(displayName, node.longName, sizeof(displayName) - 1);
    displayName[sizeof(displayName) - 1] = '\0';
    
    // Add ellipsis if truncated
    size_t nameLen = strlen(node.longName);
    if (nameLen > 15) {
        strcpy(displayName + 15, "...");
    }
    
    tft.setCursor(35, y + 3);
    tft.print(displayName);
    
    // Last heard time (second line)
    String timeStr = formatTimeSince(node.lastHeard);
    uint16_t timeColor = node.isOnline ? COLOR_GREEN : COLOR_DIM_GREEN;
    if (isSelected) {
        timeColor = node.isOnline ? 0xFFFF : 0xC618; // White or light grey when selected
    }
    
    tft.setTextColor(timeColor, bgColor);
    tft.setCursor(35, y + 12);
    tft.setTextSize(1);
    tft.print(timeStr);
    
    // Status indicators (right side)
    int rightX = 250;
    
    // Favorite star
    if (node.isFavorite) {
        tft.setTextColor(COLOR_YELLOW, bgColor);
        tft.setCursor(rightX, y + 6);
        tft.print("*");
        rightX += 10;
    }
    
    // Internet/MQTT indicator
    if (node.viaInternet) {
        uint16_t indicatorColor = isSelected ? 0x87FF : COLOR_BLUE; // Cyan when selected, blue when not
        tft.setTextColor(indicatorColor, bgColor);
        tft.setCursor(rightX, y + 6);
        tft.print("I");
        rightX += 10;
    }
    
    // Hops indicator
    if (node.hopsAway > 0) {
        uint16_t hopsColor = isSelected ? 0xFFFF : COLOR_DIM_GREEN; // White when selected
        tft.setTextColor(hopsColor, bgColor);
        tft.setCursor(rightX, y + 6);
        tft.print(node.hopsAway);
    }
    
    // SNR value (small, bottom right)
    uint16_t snrColor = isSelected ? 0xC618 : COLOR_DIM_GREEN; // Light grey when selected
    tft.setTextColor(snrColor, bgColor);
    tft.setCursor(270, y + 12);
    tft.print(node.snr, 1);
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

