#pragma once

#include <Adafruit_ST7789.h>
#include <vector>
#include <memory>
#include "BaseScreen.h"
#include "UIDataState.h"

// Forward declarations
class HomeScreen;
class NodesListScreen;

/**
 * Main UI navigation manager with efficient update system
 * Handles screen stack, navigation, and data-driven display updates
 */
class UINavigator {
public:
    UINavigator(Adafruit_ST7789& display);
    ~UINavigator();

    // Navigation methods
    void navigateTo(BaseScreen* screen);
    void navigateBack();
    void navigateHome();
    void navigateToNodes(); // Convenience method
    
    // Input handling
    void handleInput(uint8_t input);
    
    // Display management with efficiency
    void update();
    void forceRedraw();
    
    // Screen access
    BaseScreen* getCurrentScreen() const;
    bool hasScreens() const { return !screenStack.empty(); }
    
    // Display reference for screens
    Adafruit_ST7789& getDisplay() { return tft; }
    
    // Data state access
    UIDataState& getDataState() { return dataState; }

private:
    Adafruit_ST7789& tft;
    std::vector<BaseScreen*> screenStack;
    UIDataState dataState;
    unsigned long lastDataUpdate;
    unsigned long lastDisplayUpdate;
    static const unsigned long DATA_UPDATE_INTERVAL = 2000;    // Check data every 2 seconds
    static const unsigned long DISPLAY_UPDATE_INTERVAL = 100;  // Update display every 100ms if needed
    
    void cleanup();
    void initializeScreens();
    void updateData();
    void updateDisplay();
    
    // Screen instances
    HomeScreen* homeScreen;
    NodesListScreen* nodesListScreen;
};