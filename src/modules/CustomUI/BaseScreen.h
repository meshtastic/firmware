#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Arduino.h>
#include "UIDataState.h"

// Forward declaration
class UINavigator;

/**
 * Abstract base class for all UI screens
 * Provides common functionality and enforces interface for screen management
 * Supports dirty rectangle updates for efficiency
 */
class BaseScreen {
public:
    BaseScreen(UINavigator* navigator, const char* screenName) 
        : navigator(navigator), name(screenName), needsFullRedraw(true), lastUpdateTime(0) {}
    
    virtual ~BaseScreen() = default;

    // Pure virtual methods that must be implemented by derived classes
    virtual void onEnter() = 0;                    // Called when screen becomes active
    virtual void onExit() = 0;                     // Called when screen is being left
    virtual void handleInput(uint8_t input) = 0;   // Handle button press (user button = 1, others as needed)
    virtual void draw(Adafruit_ST7789& tft, UIDataState& dataState) = 0;   // Draw screen content
    virtual bool needsUpdate(UIDataState& dataState) = 0; // Return true if screen needs redraw based on data changes

    // Common functionality
    virtual void drawHeader(Adafruit_ST7789& tft, const char* title = nullptr);
    virtual void drawFooter(Adafruit_ST7789& tft, const char* footerText);
    
    // Screen management
    const char* getName() const { return name; }
    void markForFullRedraw() { needsFullRedraw = true; }
    void clearRedrawFlag() { needsFullRedraw = false; }
    bool getNeedsFullRedraw() const { return needsFullRedraw; }
    
    // Dirty rectangle support
    struct DirtyRect {
        int x, y, width, height;
        bool active;
    };
    
    void markDirtyRect(int x, int y, int width, int height);
    void clearDirtyRects();
    bool hasDirtyRects() const;

protected:
    UINavigator* navigator;
    const char* name;
    bool needsFullRedraw;
    unsigned long lastUpdateTime;
    
    // Dirty rectangles for efficient updates
    static const int MAX_DIRTY_RECTS = 4;
    DirtyRect dirtyRects[MAX_DIRTY_RECTS];
    int dirtyRectCount;
    
    // Helper methods for dirty rectangle updates
    void clearRect(Adafruit_ST7789& tft, int x, int y, int width, int height);
    void drawTextInRect(Adafruit_ST7789& tft, int x, int y, int width, int height, 
                       const char* text, uint16_t textColor, uint16_t bgColor, uint8_t textSize = 1);
    
    // Common colors
    static const uint16_t COLOR_HEADER = ST77XX_GREEN;
    static const uint16_t COLOR_TEXT = ST77XX_WHITE;
    static const uint16_t COLOR_ACCENT = ST77XX_CYAN;
    static const uint16_t COLOR_WARNING = ST77XX_YELLOW;
    static const uint16_t COLOR_ERROR = ST77XX_RED;
    static const uint16_t COLOR_SUCCESS = ST77XX_GREEN;
    static const uint16_t COLOR_BACKGROUND = ST77XX_BLACK;
    
    // Common screen areas
    static const int HEADER_HEIGHT = 25;
    static const int FOOTER_HEIGHT = 20;
    static const int CONTENT_START_Y = HEADER_HEIGHT + 5;
    static const int SCREEN_WIDTH = 320;
    static const int SCREEN_HEIGHT = 240;
    static const int CONTENT_HEIGHT = SCREEN_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT - 10;
    
    // Update intervals
    static const unsigned long MIN_UPDATE_INTERVAL = 1000; // Minimum 1 second between updates
};