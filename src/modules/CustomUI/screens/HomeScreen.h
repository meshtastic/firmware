#pragma once

#include "BaseScreen.h"
#include <LovyanGFX.hpp>

/**
 * Home screen - main screen with device status and basic info
 * Shows:
 * - Device status
 * - Node count
 * - Network status
 * - Last activity
 */
class HomeScreen : public BaseScreen {
public:
    HomeScreen();
    virtual ~HomeScreen();
    
    // Screen lifecycle
    virtual void onEnter() override;
    virtual void onExit() override;
    virtual void onDraw(lgfx::LGFX_Device& tft) override;
    
    // Input handling
    virtual bool handleKeyPress(char key) override;
    
    // Update methods
    void updateStatus();

private:
    void drawDeviceStatus(lgfx::LGFX_Device& tft);
    void drawNetworkInfo(lgfx::LGFX_Device& tft);
    void drawSystemMetrics(lgfx::LGFX_Device& tft);
    void drawLastActivity(lgfx::LGFX_Device& tft);
    
    // Status tracking
    unsigned long lastUpdate;
    String lastStatus;
    int lastNodeCount;
    bool statusChanged;

    // Layout constants
    static const int LEFT_COLUMN_X = 10;
    static const int RIGHT_COLUMN_X = 170;
    static const int COLUMN_WIDTH = 140;
    static const int LINE_HEIGHT = 18;
};