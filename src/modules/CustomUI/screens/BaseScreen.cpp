#include "BaseScreen.h"
#include "utils/BatteryHelper.h"
#include "utils/LoRaHelper.h"
#include "configuration.h"

BaseScreen::BaseScreen(const String& screenName) 
    : name(screenName), needsRedraw(true), headerNeedsUpdate(true) {
    LOG_INFO("BaseScreen '%s' created", screenName.c_str());
}

BaseScreen::~BaseScreen() {
    LOG_INFO("BaseScreen '%s' destroyed", name.c_str());
}

void BaseScreen::draw(lgfx::LGFX_Device& tft) {
    // Check if header needs updating (battery/device name changed)
    if (BatteryHelper::hasChanged() || LoRaHelper::hasChanged()) {
        headerNeedsUpdate = true;
    }
    
    if (needsRedraw) {
        // Full screen redraw - ensure black background
        tft.fillScreen(0x0000); // Pure black background
        drawHeader(tft);
        drawFooter(tft);
        onDraw(tft);  // Let derived class draw content
        needsRedraw = false;
        headerNeedsUpdate = false;
    } else if (headerNeedsUpdate) {
        // Update only header
        updateHeader(tft);
        headerNeedsUpdate = false;
    }
}

void BaseScreen::drawHeader(lgfx::LGFX_Device& tft) {
    // Clear header area
    tft.fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, 0x0000);
    
    // Draw header separator line
    tft.drawFastHLine(0, HEADER_HEIGHT - 1, SCREEN_WIDTH, 0x2104); // Dark green
    
    // Get current status
    String deviceName = LoRaHelper::getDeviceLongName();
    String batteryStatus = BatteryHelper::getBatteryString();
    
    // Device name (top left) - Green text
    tft.setTextColor(0x07E0, 0x0000); // Bright green on black
    tft.setTextSize(1);
    tft.setCursor(5, 8);
    tft.print(deviceName);
    
    // Battery status (top right)
    int16_t textWidth = tft.textWidth(batteryStatus);
    tft.setCursor(SCREEN_WIDTH - textWidth - 5, 8);
    
    // Color code battery percentage
    int batteryPercent = BatteryHelper::getBatteryPercent();
    if (batteryPercent > 50) {
        tft.setTextColor(0x07E0, 0x0000); // Green on black
    } else if (batteryPercent > 20) {
        tft.setTextColor(0xFFE0, 0x0000); // Yellow on black
    } else {
        tft.setTextColor(0x7800, 0x0000); // Dark red on black
    }
    tft.print(batteryStatus);
    
    // Update tracking
    lastDeviceName = deviceName;
    lastBatteryStatus = batteryStatus;
}

void BaseScreen::updateHeader(lgfx::LGFX_Device& tft) {
    // Get current status
    String deviceName = LoRaHelper::getDeviceLongName();
    String batteryStatus = BatteryHelper::getBatteryString();
    
    // Only update changed parts
    if (deviceName != lastDeviceName) {
        // Clear left side of header
        tft.fillRect(0, 0, SCREEN_WIDTH/2, HEADER_HEIGHT-1, 0x0000);
        
        // Draw device name - Green text
        tft.setTextColor(0x07E0, 0x0000); // Bright green on black
        tft.setTextSize(1);
        tft.setCursor(5, 8);
        tft.print(deviceName);
        
        lastDeviceName = deviceName;
    }
    
    if (batteryStatus != lastBatteryStatus) {
        // Clear right side of header
        tft.fillRect(SCREEN_WIDTH/2, 0, SCREEN_WIDTH/2, HEADER_HEIGHT-1, 0x0000);
        
        // Draw battery status
        int16_t textWidth = tft.textWidth(batteryStatus);
        tft.setCursor(SCREEN_WIDTH - textWidth - 5, 8);
        
        // Color code battery
        int batteryPercent = BatteryHelper::getBatteryPercent();
        if (batteryPercent > 50) {
            tft.setTextColor(0x07E0, 0x0000); // Green on black
        } else if (batteryPercent > 20) {
            tft.setTextColor(0xFFE0, 0x0000); // Yellow on black
        } else {
            tft.setTextColor(0x7800, 0x0000); // Dark red on black
        }
        tft.print(batteryStatus);
        
        lastBatteryStatus = batteryStatus;
    }
}

void BaseScreen::drawFooter(lgfx::LGFX_Device& tft) {
    int footerY = SCREEN_HEIGHT - FOOTER_HEIGHT;
    
    // Clear footer area
    tft.fillRect(0, footerY, SCREEN_WIDTH, FOOTER_HEIGHT, 0x0000);
    
    // Draw footer separator line
    tft.drawFastHLine(0, footerY, SCREEN_WIDTH, 0x2104); // Dark green
    
    if (navHints.empty()) {
        return; // No navigation hints to display
    }
    
    // Calculate spacing for equidistant buttons
    int numHints = navHints.size();
    int buttonWidth = SCREEN_WIDTH / numHints;
    
    tft.setTextColor(0x4208, 0x0000); // Dim green on black for navigation hints
    tft.setTextSize(1);
    
    for (int i = 0; i < numHints; i++) {
        const NavHint& hint = navHints[i];
        
        // Create button text: [1]Home
        String buttonText = "[" + String(hint.key) + "]" + hint.label;
        
        // Calculate position to center text in button area
        int16_t textWidth = tft.textWidth(buttonText);
        int x = (buttonWidth * i) + (buttonWidth - textWidth) / 2;
        int y = footerY + 8;
        
        // Draw button text
        tft.setCursor(x, y);
        tft.print(buttonText);
        
        // Draw separator line between buttons (except last)
        if (i < numHints - 1) {
            int lineX = buttonWidth * (i + 1);
            tft.drawFastVLine(lineX, footerY + 3, FOOTER_HEIGHT - 6, 0x1082); // Very dark green
        }
    }
}

void BaseScreen::setNavigationHints(const std::vector<NavHint>& hints) {
    navHints = hints;
    needsRedraw = true; // Footer changed, need full redraw
}