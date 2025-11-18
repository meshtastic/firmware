#pragma once

#include <LovyanGFX.hpp>
#include <Arduino.h>
#include <vector>

// Navigation hint structure
struct NavHint {
    char key;          // Key to press (e.g., '1', '2', '3')
    String label;      // Label to display (e.g., "Home", "Nodes")
    
    NavHint(char k, const String& l) : key(k), label(l) {}
};

/**
 * Base abstract screen class for CustomUI
 * Provides standard 3-section layout: header, content, footer
 * 
 * Layout:
 * ┌─────────────────────────────────────┐
 * │ Header: Device Name    Battery %    │ ← 30px height
 * ├─────────────────────────────────────┤
 * │                                     │
 * │            Content Area             │ ← 150px height
 * │                                     │
 * ├─────────────────────────────────────┤
 * │ Footer: [1]Home [2]Nodes [3]WiFi    │ ← 30px height
 * └─────────────────────────────────────┘
 */
class BaseScreen {
public:
    BaseScreen(const String& screenName);
    virtual ~BaseScreen();
    
    // Screen lifecycle
    virtual void onEnter() = 0;     // Called when screen becomes active
    virtual void onExit() = 0;      // Called when leaving screen
    virtual void onDraw(lgfx::LGFX_Device& tft) = 0;  // Draw content area only
    
    // Input handling
    virtual bool handleKeyPress(char key) = 0;  // Return true if key was handled
    
    // Screen management
    void draw(lgfx::LGFX_Device& tft);  // Draw complete screen (header + content + footer)
    void forceRedraw() { needsRedraw = true; headerNeedsUpdate = true; }
    bool needsUpdate() const { return needsRedraw || headerNeedsUpdate; }
    
    // Navigation
    void setNavigationHints(const std::vector<NavHint>& hints);
    
    // Screen info
    const String& getName() const { return name; }
    
    // Layout constants
    static const int HEADER_HEIGHT = 30;
    static const int FOOTER_HEIGHT = 30;
    static const int CONTENT_Y = HEADER_HEIGHT;
    static const int CONTENT_HEIGHT = 240 - HEADER_HEIGHT - FOOTER_HEIGHT; // 180px
    static const int SCREEN_WIDTH = 320;
    static const int SCREEN_HEIGHT = 240;

protected:
    String name;
    bool needsRedraw;
    bool headerNeedsUpdate;
    std::vector<NavHint> navHints;
    
    // Layout helpers for derived classes
    int getContentY() const { return CONTENT_Y; }
    int getContentHeight() const { return CONTENT_HEIGHT; }
    int getContentWidth() const { return SCREEN_WIDTH; }

private:
    void drawHeader(lgfx::LGFX_Device& tft);
    void drawFooter(lgfx::LGFX_Device& tft);
    void updateHeader(lgfx::LGFX_Device& tft);
    
    // Header state tracking
    String lastDeviceName;
    String lastBatteryStatus;
};