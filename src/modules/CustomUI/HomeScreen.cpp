#include "HomeScreen.h"
#include "UINavigator.h"
#include "NodesListScreen.h"
#include <Arduino.h>

HomeScreen::HomeScreen(UINavigator* navigator) 
    : BaseScreen(navigator, "HOME") {
}

void HomeScreen::onEnter() {
    LOG_INFO("🔧 UI: Entering Home Screen");
    // markForFullRedraw();
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
    // Set up navigation hints
    const char* hints[] = {"A:Back", "1:Nodes"};
    
    // Draw the new structured layout (header, footer, borders)
    drawFullLayout(tft, dataState, hints, 2);
    
    // Get system data for content
    const auto& systemData = dataState.getSystemData();
    
    // Draw content in rows within the content area
    drawSystemInfoRows(tft, systemData);
    drawMeshStatsRows(tft, systemData);
    drawLoRaConfigRows(tft, systemData);
    
    clearRedrawFlag();
}

void HomeScreen::drawSystemInfoRows(Adafruit_ST7789& tft, const UIDataState::SystemData& data) {
    tft.setTextColor(BaseScreen::COLOR_TEXT, BaseScreen::COLOR_BACKGROUND);
    tft.setTextSize(1);
    
    // Row 1: Network Status
    int y = BaseScreen::CONTENT_Y + (0 * BaseScreen::ROW_HEIGHT);
    tft.setCursor(BaseScreen::CONTENT_X + 4, y + 2);
    tft.print("Network: ");
    tft.setTextColor(data.isConnected ? BaseScreen::COLOR_SUCCESS : BaseScreen::COLOR_WARNING, BaseScreen::COLOR_BACKGROUND);
    tft.print(data.isConnected ? "CONNECTED" : "SEARCHING");
    
    // Row 2: Node count
    y = BaseScreen::CONTENT_Y + (1 * BaseScreen::ROW_HEIGHT);
    tft.setTextColor(BaseScreen::COLOR_TEXT, BaseScreen::COLOR_BACKGROUND);
    tft.setCursor(BaseScreen::CONTENT_X + 4, y + 2);
    tft.printf("Nodes: %d", (int)data.nodeCount);
}

void HomeScreen::drawMeshStatsRows(Adafruit_ST7789& tft, const UIDataState::SystemData& data) {
    tft.setTextColor(BaseScreen::COLOR_TEXT, BaseScreen::COLOR_BACKGROUND);
    tft.setTextSize(1);
    
    // Row 3: Memory info
    int y = BaseScreen::CONTENT_Y + (2 * BaseScreen::ROW_HEIGHT);
    tft.setCursor(BaseScreen::CONTENT_X + 4, y + 2);
    tft.printf("Free RAM: %dKB", (int)data.freeHeapKB);
    
    // Row 4: Uptime
    y = BaseScreen::CONTENT_Y + (3 * BaseScreen::ROW_HEIGHT);
    tft.setCursor(BaseScreen::CONTENT_X + 4, y + 2);
    uint32_t uptimeMinutes = data.uptime / 60;
    if (uptimeMinutes > 60) {
        uint32_t hours = uptimeMinutes / 60;
        uint32_t mins = uptimeMinutes % 60;
        tft.printf("Uptime: %dh %dm", (int)hours, (int)mins);
    } else {
        tft.printf("Uptime: %dm", (int)uptimeMinutes);
    }
}

void HomeScreen::drawLoRaConfigRows(Adafruit_ST7789& tft, const UIDataState::SystemData& data) {
    tft.setTextColor(BaseScreen::COLOR_TEXT, BaseScreen::COLOR_BACKGROUND);
    tft.setTextSize(1);
    
    // Row 5: LoRa Region
    int y = BaseScreen::CONTENT_Y + (4 * BaseScreen::ROW_HEIGHT);
    tft.setCursor(BaseScreen::CONTENT_X + 4, y + 2);
    tft.printf("LoRa Region: %d", data.loraRegion);
    
    // Row 6: LoRa Preset
    y = BaseScreen::CONTENT_Y + (5 * BaseScreen::ROW_HEIGHT);
    tft.setCursor(BaseScreen::CONTENT_X + 4, y + 2);
    tft.printf("LoRa Preset: %d", data.loraPreset);
}
