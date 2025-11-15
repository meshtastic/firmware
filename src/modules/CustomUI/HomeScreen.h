#pragma once

#include "BaseScreen.h"
#include "UIDataState.h"
#include "mesh/NodeDB.h"
#include "configuration.h"

/**
 * Home screen showing ESP32 hardware status and LoRa information
 * Uses efficient updates based on data changes only
 */
class HomeScreen : public BaseScreen {
public:
    HomeScreen(UINavigator* navigator);
    
    // BaseScreen interface
    void onEnter() override;
    void onExit() override;
    void handleInput(uint8_t input) override;
    void draw(Adafruit_ST7789& tft, UIDataState& dataState) override;
    bool needsUpdate(UIDataState& dataState) override;

private:
    // Row-based drawing functions (new layout system)
    void drawSystemInfoRows(Adafruit_ST7789& tft, const UIDataState::SystemData& data);
    void drawMeshStatsRows(Adafruit_ST7789& tft, const UIDataState::SystemData& data);
    void drawLoRaConfigRows(Adafruit_ST7789& tft, const UIDataState::SystemData& data);
};