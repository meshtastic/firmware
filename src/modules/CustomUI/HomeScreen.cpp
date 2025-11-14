#include "HomeScreen.h"
#include "UINavigator.h"
#include "NodesListScreen.h"
#include <Arduino.h>

// Define screen areas for dirty rectangle updates
const HomeScreen::AreaRect HomeScreen::areaRects[4] = {
    {5, CONTENT_START_Y, SCREEN_WIDTH - 10, 60},           // AREA_SYSTEM_INFO
    {5, CONTENT_START_Y + 65, SCREEN_WIDTH - 10, 50},      // AREA_MESH_STATS
    {5, CONTENT_START_Y + 120, SCREEN_WIDTH - 10, 60},     // AREA_LORA_CONFIG
    {5, CONTENT_START_Y + 185, SCREEN_WIDTH - 10, 25}      // AREA_BATTERY
};

HomeScreen::HomeScreen(UINavigator* navigator) 
    : BaseScreen(navigator, "HOME") {
}

void HomeScreen::onEnter() {
    LOG_INFO("🔧 UI: Entering Home Screen");
    markForFullRedraw();
}

void HomeScreen::onExit() {
    LOG_INFO("🔧 UI: Exiting Home Screen");
}

void HomeScreen::handleInput(uint8_t input) {
    switch (input) {
        case 1: // User button - navigate to nodes list
            LOG_INFO("🔧 UI: Button pressed - navigating to nodes list");
            navigator->navigateToNodes();
            break;
        default:
            LOG_DEBUG("🔧 UI: Unhandled input: %d", input);
            break;
    }
}

bool HomeScreen::needsUpdate(UIDataState& dataState) {
    // Track if this is the first draw
    static bool hasBeenDrawn = false;
    
    if (!hasBeenDrawn) {
        hasBeenDrawn = true;
        return true; // Draw once on first run
    }
    
    // After first draw, only update if system data has actually changed
    // and it's been marked as changed (not just invalid)
    if (dataState.isSystemDataChanged()) {
        // Check if data is actually different from last time
        const auto& currentData = dataState.getSystemData();
        static uint32_t lastNodeId = 0;
        static int lastBatteryPercent = -1;
        static bool lastHasBattery = false;
        
        bool dataActuallyChanged = (currentData.nodeId != lastNodeId) ||
                                  (currentData.batteryPercent != lastBatteryPercent) ||
                                  (currentData.hasBattery != lastHasBattery);
        
        if (dataActuallyChanged) {
            lastNodeId = currentData.nodeId;
            lastBatteryPercent = currentData.batteryPercent;
            lastHasBattery = currentData.hasBattery;
            return true;
        }
    }
    
    return false; // No update needed
}

void HomeScreen::draw(Adafruit_ST7789& tft, UIDataState& dataState) {
    tft.fillScreen(ST77XX_BLACK);
    
    // Get current data using public getter
    const auto& data = dataState.getSystemData();
    
    // Draw main border (like Python GUI)
    tft.drawRect(0, 0, 320, 240, ST77XX_GREEN);
    
    // Header section - hostname and status
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_GREEN);
    tft.setCursor(8, 8);
    
    // Hostname on left (uppercase like Python)
    char nodeIdStr[16];
    snprintf(nodeIdStr, sizeof(nodeIdStr), "NODE_%08X", (unsigned int)data.nodeId);
    tft.print(nodeIdStr);
    
    // Mode and battery on right
    char statusText[32];
    if (data.hasBattery) {
        snprintf(statusText, sizeof(statusText), "MESH | %d%%", data.batteryPercent);
    } else {
        snprintf(statusText, sizeof(statusText), "MESH | EXT");
    }
    
    // Right-align status text
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(statusText, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor(320 - w - 8, 8);
    tft.print(statusText);
    
    // Header separator line
    tft.drawLine(8, 25, 312, 25, ST77XX_GREEN);
    
    // IP section
    tft.setCursor(8, 35);
    tft.print("IP_v4......... 192.168.1.XXX"); // Placeholder - would need actual IP
    
    // System status section header
    tft.setCursor(8, 55);
    tft.print("SYS_STATUS:");
    
    // System status box (like Python GUI)
    tft.drawRect(8, 70, 304, 50, ST77XX_GREEN);
    
    // System stats inside box
    tft.setCursor(16, 78);
    // Show LoRa status instead of CPU
    const char* loraStatus = data.isConnected ? "CONNECTED" : "SEARCHING";
    tft.printf("LORA >> %s", loraStatus);
    
    tft.setCursor(16, 95);
    tft.printf("MEM >> %lu KB", data.freeHeapKB);
    
    // Kernel info on right side of box
    tft.setCursor(200, 78);
    tft.print("[ KERNEL ]");
    tft.setCursor(200, 95);
    tft.print("ESP32-S3");
    
    // Time section at bottom (centered like Python)
    char timeStr[32];
    uint32_t seconds = data.uptime / 1000;
    uint32_t hours = seconds / 3600;
    uint32_t minutes = (seconds % 3600) / 60;
    uint32_t secs = seconds % 60;
    snprintf(timeStr, sizeof(timeStr), "[ %02d:%02d:%02d ]", (int)hours, (int)minutes, (int)secs);
    
    // Center the time
    tft.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((320 - w) / 2, 145);
    tft.print(timeStr);
    
    // Footer with navigation hints
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(8, 220);
    tft.print("[BTN] Menu  [HOLD] Nodes");
}

void HomeScreen::drawSystemInfo(Adafruit_ST7789& tft, const UIDataState::SystemData& data, bool forceRedraw) {
    if (!forceRedraw) return; // For now, we'll implement fine-grained dirty checking later
    
    int y = CONTENT_START_Y;
    const auto& rect = areaRects[AREA_SYSTEM_INFO];
    
    // Clear area
    if (!forceRedraw) {
        tft.fillRect(rect.x, rect.y, rect.width, rect.height, COLOR_BACKGROUND);
    }
    
    // Node information
    tft.setTextColor(COLOR_ACCENT, COLOR_BACKGROUND);
    tft.setTextSize(1);
    tft.setCursor(5, y);
    tft.print("NODE:");
    
    tft.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    tft.setCursor(50, y);
    if (strlen(data.shortName) > 0) {
        tft.print(data.shortName);
    } else {
        char nodeStr[16];
        snprintf(nodeStr, sizeof(nodeStr), "%08X", data.nodeId);
        tft.print(nodeStr);
    }
    
    y += 15;
    
    // Uptime (only show hours and minutes)
    tft.setTextColor(COLOR_ACCENT, COLOR_BACKGROUND);
    tft.setCursor(5, y);
    tft.print("UPTIME:");
    tft.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    tft.setCursor(65, y);
    
    uint32_t hours = data.uptime / 3600;
    uint32_t minutes = (data.uptime % 3600) / 60;
    
    char uptimeStr[32];
    snprintf(uptimeStr, sizeof(uptimeStr), "%luh %lum", hours, minutes);
    tft.print(uptimeStr);
    
    y += 15;
    
    // ESP32 info
    tft.setTextColor(COLOR_ACCENT, COLOR_BACKGROUND);
    tft.setCursor(5, y);
    tft.print("CHIP:");
    tft.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    tft.setCursor(45, y);
    tft.print("ESP32-S3");
    
    y += 15;
    
    // Free heap
    tft.setTextColor(COLOR_ACCENT, COLOR_BACKGROUND);
    tft.setCursor(5, y);
    tft.print("HEAP:");
    tft.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    tft.setCursor(45, y);
    
    char heapStr[16];
    snprintf(heapStr, sizeof(heapStr), "%lu KB", data.freeHeapKB);
    tft.print(heapStr);
}

void HomeScreen::drawMeshStats(Adafruit_ST7789& tft, const UIDataState::SystemData& data, bool forceRedraw) {
    if (!forceRedraw) return;
    
    int y = CONTENT_START_Y + 65;
    
    // Section header
    tft.setTextColor(COLOR_HEADER, COLOR_BACKGROUND);
    tft.setTextSize(1);
    tft.setCursor(5, y);
    tft.print("MESH NETWORK");
    tft.drawLine(5, y + 12, SCREEN_WIDTH - 5, y + 12, COLOR_HEADER);
    
    y += 20;
    
    // Node count
    tft.setTextColor(COLOR_ACCENT, COLOR_BACKGROUND);
    tft.setCursor(5, y);
    tft.print("NODES:");
    tft.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    tft.setCursor(55, y);
    tft.print(data.nodeCount);
    
    y += 15;
    
    // Connection status
    tft.setTextColor(COLOR_ACCENT, COLOR_BACKGROUND);
    tft.setCursor(5, y);
    tft.print("STATUS:");
    tft.setTextColor(data.isConnected ? COLOR_SUCCESS : COLOR_WARNING, COLOR_BACKGROUND);
    tft.setCursor(65, y);
    tft.print(data.isConnected ? "CONNECTED" : "SEARCHING");
}

void HomeScreen::drawLoRaConfig(Adafruit_ST7789& tft, const UIDataState::SystemData& data, bool forceRedraw) {
    if (!forceRedraw) return;
    
    int y = CONTENT_START_Y + 120;
    
    // Section header
    tft.setTextColor(COLOR_HEADER, COLOR_BACKGROUND);
    tft.setTextSize(1);
    tft.setCursor(5, y);
    tft.print("LORA CONFIG");
    tft.drawLine(5, y + 12, SCREEN_WIDTH - 5, y + 12, COLOR_HEADER);
    
    y += 20;
    
    // Region
    tft.setTextColor(COLOR_ACCENT, COLOR_BACKGROUND);
    tft.setCursor(5, y);
    tft.print("REGION:");
    tft.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    tft.setCursor(65, y);
    
    // Convert region to string (simplified)
    const char* regionStr = "UNKNOWN";
    switch (data.loraRegion) {
        case 1: regionStr = "US"; break;
        case 2: regionStr = "EU_433"; break;
        case 3: regionStr = "EU_868"; break;
        case 4: regionStr = "CN"; break;
        case 5: regionStr = "JP"; break;
        case 6: regionStr = "ANZ"; break;
        case 7: regionStr = "KR"; break;
        case 8: regionStr = "TW"; break;
        case 9: regionStr = "RU"; break;
        case 10: regionStr = "IN"; break;
        default: break;
    }
    tft.print(regionStr);
    
    y += 15;
    
    // Modem preset
    tft.setTextColor(COLOR_ACCENT, COLOR_BACKGROUND);
    tft.setCursor(5, y);
    tft.print("PRESET:");
    tft.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    tft.setCursor(65, y);
    
    const char* presetStr = "UNKNOWN";
    switch (data.loraPreset) {
        case 0: presetStr = "LONG_FAST"; break;
        case 1: presetStr = "LONG_SLOW"; break;
        case 2: presetStr = "VERY_LONG_SLOW"; break;
        case 3: presetStr = "MEDIUM_SLOW"; break;
        case 4: presetStr = "MEDIUM_FAST"; break;
        case 5: presetStr = "SHORT_SLOW"; break;
        case 6: presetStr = "SHORT_FAST"; break;
        case 7: presetStr = "LONG_MODERATE"; break;
        default: break;
    }
    tft.print(presetStr);
}

void HomeScreen::drawBatteryInfo(Adafruit_ST7789& tft, const UIDataState::SystemData& data, bool forceRedraw) {
    if (!forceRedraw) return;
    
    int y = CONTENT_START_Y + 185;
    
    // Battery section
    tft.setTextColor(COLOR_ACCENT, COLOR_BACKGROUND);
    tft.setTextSize(1);
    tft.setCursor(5, y);
    tft.print("POWER:");
    
    if (data.hasBattery) {
        // Show battery percentage with color coding
        uint16_t batteryColor = COLOR_SUCCESS;
        if (data.batteryPercent < 20) {
            batteryColor = COLOR_ERROR;
        } else if (data.batteryPercent < 50) {
            batteryColor = COLOR_WARNING;
        }
        
        tft.setTextColor(batteryColor, COLOR_BACKGROUND);
        tft.setCursor(50, y);
        char battStr[16];
        snprintf(battStr, sizeof(battStr), "%d%% BATT", data.batteryPercent);
        tft.print(battStr);
        
        // Draw simple battery icon
        int battX = 200;
        int battY = y;
        int battW = 30;
        int battH = 10;
        
        // Battery outline
        tft.drawRect(battX, battY, battW, battH, COLOR_TEXT);
        tft.drawRect(battX + battW, battY + 2, 3, battH - 4, COLOR_TEXT);
        
        // Battery fill
        int fillW = (battW - 2) * data.batteryPercent / 100;
        if (fillW > 0) {
            tft.fillRect(battX + 1, battY + 1, fillW, battH - 2, batteryColor);
        }
    } else {
        tft.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
        tft.setCursor(50, y);
        tft.print("EXTERNAL");
    }
}