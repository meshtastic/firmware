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
    // Screen areas for dirty rectangle updates
    enum ScreenArea {
        AREA_SYSTEM_INFO = 0,
        AREA_MESH_STATS = 1,
        AREA_LORA_CONFIG = 2,
        AREA_BATTERY = 3
    };
    
    void drawSystemInfo(Adafruit_ST7789& tft, const UIDataState::SystemData& data, bool forceRedraw = false);
    void drawMeshStats(Adafruit_ST7789& tft, const UIDataState::SystemData& data, bool forceRedraw = false);
    void drawLoRaConfig(Adafruit_ST7789& tft, const UIDataState::SystemData& data, bool forceRedraw = false);
    void drawBatteryInfo(Adafruit_ST7789& tft, const UIDataState::SystemData& data, bool forceRedraw = false);
    
    // Area coordinates for dirty rectangle optimization
    struct AreaRect {
        int x, y, width, height;
    };
    
    static const AreaRect areaRects[4];
};