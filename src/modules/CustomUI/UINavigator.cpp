#include "UINavigator.h"
#include "HomeScreen.h"
#include "NodesListScreen.h"
#include <Arduino.h>

UINavigator::UINavigator(Adafruit_ST7789& display) 
    : tft(display), lastDataUpdate(0), lastDisplayUpdate(0), homeScreen(nullptr), nodesListScreen(nullptr) {
    initializeScreens();
    
    // Start with home screen
    if (homeScreen) {
        navigateTo(homeScreen);
    }
}

UINavigator::~UINavigator() {
    cleanup();
}

void UINavigator::initializeScreens() {
    // Create screen instances
    homeScreen = new HomeScreen(this);
    nodesListScreen = new NodesListScreen(this);
    
    LOG_INFO("🔧 UI: Screen instances created");
}

void UINavigator::navigateTo(BaseScreen* screen) {
    if (!screen) return;
    
    // Exit current screen
    if (!screenStack.empty()) {
        screenStack.back()->onExit();
    }
    
    // Add new screen to stack
    screenStack.push_back(screen);
    screen->onEnter();
    screen->markForFullRedraw();
    
    LOG_INFO("🔧 UI: Navigated to %s", screen->getName());
}

void UINavigator::navigateBack() {
    if (screenStack.size() <= 1) {
        return; // Can't go back from home screen
    }
    
    // Exit current screen
    screenStack.back()->onExit();
    screenStack.pop_back();
    
    // Re-enter previous screen
    if (!screenStack.empty()) {
        screenStack.back()->onEnter();
        screenStack.back()->markForFullRedraw();
        LOG_INFO("🔧 UI: Navigated back to %s", screenStack.back()->getName());
    }
}

void UINavigator::navigateHome() {
    // Clear stack except home screen
    while (screenStack.size() > 1) {
        screenStack.back()->onExit();
        screenStack.pop_back();
    }
    
    // Ensure home screen is active
    if (screenStack.empty() && homeScreen) {
        navigateTo(homeScreen);
    } else if (!screenStack.empty()) {
        screenStack.back()->onEnter();
        screenStack.back()->markForFullRedraw();
    }
    
    LOG_INFO("🔧 UI: Navigated to home");
}

void UINavigator::navigateToNodes() {
    if (nodesListScreen) {
        navigateTo(nodesListScreen);
    }
}

void UINavigator::handleInput(uint8_t input) {
    if (!screenStack.empty()) {
        screenStack.back()->handleInput(input);
    }
}

void UINavigator::update() {
    unsigned long currentTime = millis();
    
    // Update data periodically
    if (currentTime - lastDataUpdate >= DATA_UPDATE_INTERVAL) {
        updateData();
        lastDataUpdate = currentTime;
    }
    
    // Update display if needed
    if (currentTime - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
        updateDisplay();
        lastDisplayUpdate = currentTime;
    }
}

void UINavigator::updateData() {
    // Update system data
    dataState.updateSystemData();
    dataState.updateNodesData();
}

void UINavigator::updateDisplay() {
    if (screenStack.empty()) return;
    
    BaseScreen* currentScreen = screenStack.back();
    
    // Check if screen needs update based on data changes or forced redraw
    bool needsFullRedraw = currentScreen->getNeedsFullRedraw();
    bool needsDataUpdate = currentScreen->needsUpdate(dataState);
    
    if (needsFullRedraw || needsDataUpdate) {
        currentScreen->draw(tft, dataState);
        currentScreen->clearRedrawFlag();
        currentScreen->clearDirtyRects();
        
        // Mark data as processed after ANY draw to prevent immediate refresh
        dataState.markSystemDataProcessed();
        dataState.markNodesDataProcessed();
    }
}

void UINavigator::forceRedraw() {
    if (!screenStack.empty()) {
        screenStack.back()->markForFullRedraw();
        screenStack.back()->draw(tft, dataState);
        screenStack.back()->clearRedrawFlag();
        
        // Mark data as processed
        dataState.markSystemDataProcessed();
        dataState.markNodesDataProcessed();
    }
}

BaseScreen* UINavigator::getCurrentScreen() const {
    return screenStack.empty() ? nullptr : screenStack.back();
}

void UINavigator::cleanup() {
    // Exit all screens
    for (auto* screen : screenStack) {
        screen->onExit();
    }
    screenStack.clear();
    
    // Delete screen instances
    delete homeScreen;
    delete nodesListScreen;
    homeScreen = nullptr;
    nodesListScreen = nullptr;
}