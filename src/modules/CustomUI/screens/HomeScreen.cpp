#include "HomeScreen.h"
#include "utils/BatteryHelper.h"
#include "utils/LoRaHelper.h"
#include "utils/DeviceMetricsHelper.h"
#include "configuration.h"

HomeScreen::HomeScreen() : BaseScreen("Home"), lastUpdate(0), lastNodeCount(-1), statusChanged(true) {
    // Set navigation hints for home screen
    std::vector<NavHint> hints;
    hints.push_back(NavHint('1', "Home"));
    hints.push_back(NavHint('3', "Snake"));
    hints.push_back(NavHint('7', "Nodes"));
    setNavigationHints(hints);
    
    // Initialize device metrics
    DeviceMetricsHelper::init();
    
    LOG_INFO("HomeScreen created");
}

HomeScreen::~HomeScreen() {
    LOG_INFO("HomeScreen destroyed");
}

void HomeScreen::onEnter() {
    LOG_INFO("Entering Home screen");
    statusChanged = true;
    lastUpdate = 0;
    forceRedraw();
}

void HomeScreen::onExit() {
    LOG_INFO("Exiting Home screen");
}

void HomeScreen::onDraw(lgfx::LGFX_Device& tft) {
    // Update status if needed
    unsigned long now = millis();
    if (now - lastUpdate > 5000) { // Update every 5 seconds
        updateStatus();
        lastUpdate = now;
    }
    
    // Clear content area to pure black
    tft.fillRect(0, getContentY(), getContentWidth(), getContentHeight(), 0x0000);
    
    // Draw border around content area
    tft.drawRect(5, getContentY() + 5, getContentWidth() - 10, getContentHeight() - 10, 0xFFE0);
    
    // Draw content in two-column layout
    drawDeviceStatus(tft);
    drawNetworkInfo(tft);
    drawSystemMetrics(tft);
    drawLastActivity(tft);
}

bool HomeScreen::handleKeyPress(char key) {
    switch (key) {
        case '1':
            // Already on home
            return true;
            
        case '3':
            // Let global navigation handle Snake game
            return false;
            
        case '7':
            // Let global navigation handle this
            return false;
            
        default:
            return false; // Key not handled
    }
}

void HomeScreen::updateStatus() {
    // Check if any status has changed
    int currentNodeCount = LoRaHelper::getNodeCount();
    
    if (currentNodeCount != lastNodeCount || 
        BatteryHelper::hasChanged() || 
        LoRaHelper::hasChanged() ||
        DeviceMetricsHelper::hasChanged()) {
        
        statusChanged = true;
        lastNodeCount = currentNodeCount;
    }
}

void HomeScreen::drawDeviceStatus(lgfx::LGFX_Device& tft) {
    int y = getContentY() + 15;
    
    // Device status section - Left column
    tft.setTextColor(0xFFE0, 0x0000); // Bright yellow on black
    tft.setTextSize(1);
    tft.setCursor(LEFT_COLUMN_X, y);
    tft.print("DEVICE:");
    y += LINE_HEIGHT;
    
    // Device name (truncated if needed)
    tft.setTextColor(0x07E0, 0x0000); // Bright green on black
    tft.setCursor(LEFT_COLUMN_X + 5, y);
    String deviceName = LoRaHelper::getDeviceLongName();
    if (deviceName.length() > 18) {
        deviceName = deviceName.substring(0, 15) + "...";
    }
    tft.print(deviceName);
    y += LINE_HEIGHT;
    
    // Battery info with color coding
    tft.setCursor(LEFT_COLUMN_X + 5, y);
    String batteryInfo = "BAT: " + BatteryHelper::getBatteryString();
    int batteryPercent = BatteryHelper::getBatteryPercent();
    
    if (batteryPercent > 50) {
        tft.setTextColor(0x07E0, 0x0000); // Green
    } else if (batteryPercent > 20) {
        tft.setTextColor(0xFFE0, 0x0000); // Yellow
    } else {
        tft.setTextColor(0xF800, 0x0000); // Red
    }
    tft.print(batteryInfo);
}

void HomeScreen::drawNetworkInfo(lgfx::LGFX_Device& tft) {
    int y = getContentY() + 75;
    
    // Network section - Left column
    tft.setTextColor(0xFFE0, 0x0000); // Bright yellow on black
    tft.setTextSize(1);
    tft.setCursor(LEFT_COLUMN_X, y);
    tft.print("NETWORK:");
    y += LINE_HEIGHT;
    
    // Node count
    tft.setTextColor(0x07E0, 0x0000); // Bright green on black
    tft.setCursor(LEFT_COLUMN_X + 5, y);
    String nodeInfo = "Nodes: " + String(LoRaHelper::getNodeCount());
    tft.print(nodeInfo);
    y += LINE_HEIGHT;
    
    // LoRa status
    tft.setCursor(LEFT_COLUMN_X + 5, y);
    if (LoRaHelper::getNodeCount() > 0) {
        tft.setTextColor(0x07E0, 0x0000); // Green
        tft.print("LoRa: Connected");
    } else {
        tft.setTextColor(0xFFE0, 0x0000); // Yellow
        tft.print("LoRa: Searching");
    }
}

void HomeScreen::drawSystemMetrics(lgfx::LGFX_Device& tft) {
    int y = getContentY() + 15;
    
    // System metrics section - Right column
    tft.setTextColor(0xFFE0, 0x0000); // Bright yellow on black
    tft.setTextSize(1);
    tft.setCursor(RIGHT_COLUMN_X, y);
    tft.print("SYSTEM:");
    y += LINE_HEIGHT;
    
    // Memory utilization with compact format
    tft.setCursor(RIGHT_COLUMN_X + 5, y);
    int memoryPercent = DeviceMetricsHelper::getMemoryUtilization();
    String memInfo = "RAM >> " + String(memoryPercent) + "%";
    
    // Color code memory based on utilization
    if (memoryPercent < 70) {
        tft.setTextColor(0x07E0, 0x0000); // Green - Good
    } else if (memoryPercent < 85) {
        tft.setTextColor(0xFFE0, 0x0000); // Yellow - Warning
    } else {
        tft.setTextColor(0xF800, 0x0000); // Red - Critical
    }
    tft.print(memInfo);
    y += LINE_HEIGHT;
    
    // Free memory details
    tft.setTextColor(0x4208, 0x0000); // Dim green
    tft.setCursor(RIGHT_COLUMN_X + 5, y);
    size_t freeHeap = DeviceMetricsHelper::getFreeHeap();
    String freeInfo = "Free: ";
    if (freeHeap >= 1024) {
        freeInfo += String(freeHeap / 1024) + "KB";
    } else {
        freeInfo += String(freeHeap) + "B";
    }
    tft.print(freeInfo);
    y += LINE_HEIGHT;
    
    // Draw border around system metrics
    tft.drawRect(RIGHT_COLUMN_X - 3, getContentY() + 12, COLUMN_WIDTH - 10, 65, 0xFFE0);
}

void HomeScreen::drawLastActivity(lgfx::LGFX_Device& tft) {
    int y = getContentY() + 135;
    
    // Activity section - Spans both columns at bottom
    tft.setTextColor(0xFFE0, 0x0000); // Bright yellow on black
    tft.setTextSize(1);
    tft.setCursor(LEFT_COLUMN_X, y);
    tft.print("UPTIME:");
    
    // Uptime - Center aligned
    tft.setTextColor(0x4208, 0x0000); // Dim green
    unsigned long uptimeSeconds = millis() / 1000;
    unsigned long hours = uptimeSeconds / 3600;
    unsigned long minutes = (uptimeSeconds % 3600) / 60;
    unsigned long seconds = uptimeSeconds % 60;
    
    String uptimeStr = String(hours) + "h " + String(minutes) + "m " + String(seconds) + "s";
    
    // Center the uptime string
    int textWidth = uptimeStr.length() * 6; // Approximate character width
    int centerX = (getContentWidth() - textWidth) / 2;
    tft.setCursor(centerX, y + LINE_HEIGHT);
    tft.print(uptimeStr);
}