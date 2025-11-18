#include "WiFiListScreen.h"
#include <Arduino.h>
#include <algorithm>

// Logging macro
#ifndef LOG_INFO
#define LOG_INFO(format, ...) Serial.printf("[INFO] " format "\n", ##__VA_ARGS__)
#endif

WiFiListScreen::WiFiListScreen() : BaseScreen("WiFi Networks") {
    // Set navigation hints
    std::vector<NavHint> hints;
    hints.push_back(NavHint('A', "Back"));
    hints.push_back(NavHint('1', "Select"));
    setNavigationHints(hints);
    
    selectedIndex = 0;
    scrollOffset = 0;
    maxVisibleItems = 8; // 8 networks visible in content area (180px / 20px per item = 9, leave some margin)
    isScanning = false;
    lastScanTime = 0;
    
    LOG_INFO("📶 WiFiListScreen: Created");
}

WiFiListScreen::~WiFiListScreen() {
    LOG_INFO("📶 WiFiListScreen: Destroyed");
}

void WiFiListScreen::onEnter() {
    LOG_INFO("📶 WiFiListScreen: Entering screen");
    
    // Initialize state for fresh entry
    selectedIndex = 0;
    scrollOffset = 0;
    networks.clear();
    isScanning = false;
    
    // Force immediate redraw to show screen first
    forceRedraw();
    
    // Start scanning on next update cycle (non-blocking)
    lastScanTime = 0; // This will trigger scan in onDraw
    
    LOG_INFO("📶 WiFiListScreen: Screen ready, scan will start on next update");
}

void WiFiListScreen::onExit() {
    LOG_INFO("📶 WiFiListScreen: Exiting screen");
}

void WiFiListScreen::onDraw(lgfx::LGFX_Device& tft) {
    // Check if we need to start scanning (delayed from onEnter)
    if (!isScanning && networks.empty() && lastScanTime == 0) {
        // Start scan immediately but show scanning state first
        scanForNetworks();
    }
    
    // Check for scan completion if currently scanning
    if (isScanning) {
        checkScanComplete();
    }
    
    // Clear content area
    tft.fillRect(0, getContentY(), getContentWidth(), getContentHeight(), COLOR_BLACK);
    
    if (isScanning) {
        // Show scanning message
        tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
        tft.setTextSize(1);
        tft.setCursor(10, getContentY() + 20);
        tft.print("Scanning for networks...");
        
        // Simple scanning animation
        unsigned long now = millis();
        int dots = (now / 500) % 4;
        for (int i = 0; i < dots; i++) {
            tft.print(".");
        }
        return;
    }
    
    if (networks.empty()) {
        // Show no networks message
        tft.setTextColor(COLOR_DARK_RED, COLOR_BLACK);
        tft.setTextSize(1);
        tft.setCursor(10, getContentY() + 20);
        tft.print("No networks found");
        
        tft.setTextColor(COLOR_DIM_GREEN, COLOR_BLACK);
        tft.setCursor(10, getContentY() + 40);
        tft.print("Press [#] to rescan");
        return;
    }
    
    // Draw network list
    drawNetworkList(tft);
}

bool WiFiListScreen::handleKeyPress(char key) {
    LOG_INFO("📶 WiFiListScreen: Key pressed: %c (isScanning: %s, networks: %d)", 
        key, isScanning ? "true" : "false", networks.size());
    
    if (isScanning) {
        return true; // Ignore keys while scanning
    }
    
    switch (key) {
        case 'A':
        case 'a':
            LOG_INFO("📶 WiFiListScreen: Back button pressed");
            // Will be handled by CustomUIModule for navigation back
            return false;
            
        case '1':
            if (!networks.empty()) {
                LOG_INFO("📶 WiFiListScreen: Select pressed for network: %s", 
                    networks[selectedIndex].ssid.c_str());
                // TODO: Implement network selection/connection
            }
            return true;
            
        case '2': // Up arrow
            LOG_INFO("📶 WiFiListScreen: Scroll up - current: %d", selectedIndex);
            scrollUp();
            return true;
            
        case '8': // Down arrow  
            LOG_INFO("📶 WiFiListScreen: Scroll down - current: %d", selectedIndex);
            scrollDown();
            return true;
            
        case '#':
            LOG_INFO("📶 WiFiListScreen: Rescanning networks");
            scanForNetworks();
            return true;
            
        default:
            return false;
    }
}

void WiFiListScreen::scanForNetworks() {
    LOG_INFO("📶 WiFiListScreen: Starting network scan");
    isScanning = true;
    lastScanTime = millis();
    
    // Clear previous results
    networks.clear();
    selectedIndex = 0;
    scrollOffset = 0;
    
    // Force redraw to show scanning state
    forceRedraw();
    
    // Do synchronous scan for now (async has issues)
    networks = wifiHelper.scanNetworks(15);
    isScanning = false;
    
    LOG_INFO("📶 WiFiListScreen: Scan completed, found %d networks", networks.size());
    forceRedraw(); // Update display with results
}

void WiFiListScreen::checkScanComplete() {
    // No longer needed with synchronous scanning
    // This method is kept for future async implementation
    if (!isScanning) return;
    
    LOG_INFO("📶 WiFiListScreen: checkScanComplete called, but using sync scan");
}

void WiFiListScreen::drawNetworkList(lgfx::LGFX_Device& tft) {
    int contentY = getContentY();
    int y = contentY + 5;
    
    // Calculate visible range
    int endIndex = std::min((int)networks.size(), scrollOffset + maxVisibleItems);
    
    // Draw scroll indicators if needed
    if (scrollOffset > 0) {
        tft.setTextColor(COLOR_DIM_GREEN, COLOR_BLACK);
        tft.setCursor(300, contentY + 2);
        tft.print("^");
    }
    
    if (endIndex < networks.size()) {
        tft.setTextColor(COLOR_DIM_GREEN, COLOR_BLACK);
        tft.setCursor(300, contentY + getContentHeight() - 10);
        tft.print("v");
    }
    
    // Draw visible networks
    for (int i = scrollOffset; i < endIndex; i++) {
        bool isSelected = (i == selectedIndex);
        drawNetworkEntry(tft, i, y, isSelected);
        y += ITEM_HEIGHT;
    }
}

void WiFiListScreen::drawNetworkEntry(lgfx::LGFX_Device& tft, int index, int y, bool isSelected) {
    const WiFiNetworkInfo& network = networks[index];
    
    // Selection highlight
    if (isSelected) {
        tft.fillRect(5, y - 2, getContentWidth() - 10, ITEM_HEIGHT - 2, COLOR_DIM_GREEN);
    }
    
    // Signal strength bars (first 20px)
    int bars = wifiHelper.getSignalBars(network.rssi);
    drawSignalBars(tft, 8, y + 2, bars);
    
    // SSID (main area)
    uint16_t textColor = isSelected ? COLOR_YELLOW : COLOR_GREEN;
    tft.setTextColor(textColor, isSelected ? COLOR_DIM_GREEN : COLOR_BLACK);
    tft.setTextSize(1);
    
    String displaySSID = network.ssid;
    if (displaySSID.length() > 20) {
        displaySSID = displaySSID.substring(0, 17) + "...";
    }
    
    tft.setCursor(35, y + 6);
    tft.print(displaySSID);
    
    // Security indicator (right side)
    uint16_t secColor = network.isOpen ? COLOR_DARK_RED : COLOR_GREEN;
    if (isSelected) secColor = COLOR_YELLOW;
    
    tft.setTextColor(secColor, isSelected ? COLOR_DIM_GREEN : COLOR_BLACK);
    tft.setCursor(220, y + 6);
    
    if (network.isOpen) {
        tft.print("Open");
    } else {
        String sec = network.security;
        if (sec.length() > 6) {
            sec = sec.substring(0, 6);
        }
        tft.print(sec);
    }
    
    // Signal strength text (small, right aligned)
    tft.setTextColor(COLOR_DIM_GREEN, isSelected ? COLOR_DIM_GREEN : COLOR_BLACK);
    tft.setCursor(270, y + 6);
    tft.print(network.rssi);
}

void WiFiListScreen::drawSignalBars(lgfx::LGFX_Device& tft, int x, int y, int bars) {
    // Draw 4 possible bars, fill based on signal strength
    for (int i = 0; i < 4; i++) {
        int barHeight = 2 + (i * 2); // 2, 4, 6, 8 pixels high
        int barY = y + 12 - barHeight;
        int barX = x + (i * 3);
        
        uint16_t color = (i < bars) ? COLOR_GREEN : COLOR_DIM_GREEN;
        tft.fillRect(barX, barY, 2, barHeight, color);
    }
}

void WiFiListScreen::scrollUp() {
    LOG_INFO("📶 WiFiListScreen: scrollUp called - selectedIndex: %d, networks: %d", selectedIndex, networks.size());
    if (selectedIndex > 0) {
        selectedIndex--;
        LOG_INFO("📶 WiFiListScreen: scrollUp - new selectedIndex: %d", selectedIndex);
        updateSelection();
        forceRedraw();
    } else {
        LOG_INFO("📶 WiFiListScreen: scrollUp - already at top");
    }
}

void WiFiListScreen::scrollDown() {
    LOG_INFO("📶 WiFiListScreen: scrollDown called - selectedIndex: %d, networks: %d", selectedIndex, networks.size());
    if (selectedIndex < (int)networks.size() - 1) {
        selectedIndex++;
        LOG_INFO("📶 WiFiListScreen: scrollDown - new selectedIndex: %d", selectedIndex);
        updateSelection();
        forceRedraw();
    } else {
        LOG_INFO("📶 WiFiListScreen: scrollDown - already at bottom");
    }
}

void WiFiListScreen::updateSelection() {
    // Adjust scroll offset to keep selection visible
    if (selectedIndex < scrollOffset) {
        scrollOffset = selectedIndex;
    } else if (selectedIndex >= scrollOffset + maxVisibleItems) {
        scrollOffset = selectedIndex - maxVisibleItems + 1;
    }
    
    // Ensure scroll offset is within bounds
    scrollOffset = std::max(0, std::min(scrollOffset, (int)networks.size() - maxVisibleItems));
    if (scrollOffset < 0) scrollOffset = 0;
}