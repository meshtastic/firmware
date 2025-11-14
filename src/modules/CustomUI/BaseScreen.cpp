#include "BaseScreen.h"
#include "UINavigator.h"
#include "configuration.h"
#include <Arduino.h>

void BaseScreen::drawHeader(Adafruit_ST7789& tft, const char* title) {
    // Draw header background
    tft.fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_HEADER);
    
    // Draw title
    tft.setTextColor(COLOR_BACKGROUND, COLOR_HEADER);
    tft.setTextSize(1);
    tft.setCursor(5, 8);
    tft.print("MESHTASTIC - ");
    tft.print(title ? title : name);
    
    // Draw separator line
    tft.drawLine(0, HEADER_HEIGHT, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_HEADER);
}

void BaseScreen::drawFooter(Adafruit_ST7789& tft, const char* footerText) {
    int footerY = SCREEN_HEIGHT - FOOTER_HEIGHT;
    
    // Draw separator line
    tft.drawLine(0, footerY, SCREEN_WIDTH, footerY, COLOR_HEADER);
    
    // Draw footer background
    tft.fillRect(0, footerY + 1, SCREEN_WIDTH, FOOTER_HEIGHT - 1, COLOR_BACKGROUND);
    
    // Draw footer text
    tft.setTextColor(COLOR_HEADER, COLOR_BACKGROUND);
    tft.setTextSize(1);
    
    // Center the footer text
    int textWidth = strlen(footerText) * 6; // Approximate character width
    int centerX = (SCREEN_WIDTH - textWidth) / 2;
    tft.setCursor(centerX, footerY + 6);
    tft.print(footerText);
}

void BaseScreen::markDirtyRect(int x, int y, int width, int height) {
    if (dirtyRectCount < MAX_DIRTY_RECTS) {
        dirtyRects[dirtyRectCount] = {x, y, width, height, true};
        dirtyRectCount++;
    } else {
        // If we have too many dirty rects, just do a full redraw
        markForFullRedraw();
    }
}

void BaseScreen::clearDirtyRects() {
    dirtyRectCount = 0;
    for (int i = 0; i < MAX_DIRTY_RECTS; i++) {
        dirtyRects[i].active = false;
    }
}

bool BaseScreen::hasDirtyRects() const {
    return dirtyRectCount > 0;
}

void BaseScreen::clearRect(Adafruit_ST7789& tft, int x, int y, int width, int height) {
    tft.fillRect(x, y, width, height, COLOR_BACKGROUND);
}

void BaseScreen::drawTextInRect(Adafruit_ST7789& tft, int x, int y, int width, int height, 
                               const char* text, uint16_t textColor, uint16_t bgColor, uint8_t textSize) {
    // Clear the rectangle first
    tft.fillRect(x, y, width, height, bgColor);
    
    // Draw the text
    tft.setTextColor(textColor, bgColor);
    tft.setTextSize(textSize);
    tft.setCursor(x, y);
    tft.print(text);
}