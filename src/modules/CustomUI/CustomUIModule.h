#pragma once

#include "configuration.h"

#if defined(VARIANT_heltec_v3_custom)

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include <LovyanGFX.hpp>
#include <Keypad.h>
#include <Arduino.h>
#include <vector>
#include <memory>

// Forward declarations
class InitBase;
class InitDisplay;
class InitKeypad;
class BaseScreen;
class HomeScreen;
class NodesListScreen;

#include "screens/MessagesScreen.h"
/**
 * Modular Custom UI Module for external ST7789 display with LovyanGFX
 * Architecture:
 * - Modular initializers in init/ directory (initialization only)
 * - Screen-based UI with BaseScreen abstract class
 * - CustomUIModule handles navigation and input routing
 * - LovyanGFX with automatic PSRAM, DMA, and high-speed SPI (80MHz)
 * - 4x4 keypad for navigation and input
 * - Extensible for future screens and components
 * 
 * Performance targets:
 * - 40-60 FPS with optimized drawing
 * - ~150-220KB free memory
 * - Smooth screen transitions
 */
class MessagesScreen; // Forward declaration

class CustomUIModule : public SinglePortModule, private concurrency::OSThread {
public:
    CustomUIModule();
    virtual ~CustomUIModule();
    
    // Module interface
    virtual int32_t runOnce() override;
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    virtual bool wantUIFrame() override;
    
    // Initialization
    void initAll();

private:
    // Modular initializers
    std::vector<std::unique_ptr<InitBase>> initializers;
    
    // Component references for easy access
    InitDisplay* displayInit;
    InitKeypad* keypadInit;
    
    bool allInitialized;
    
    // Display and input handling
    lgfx::LGFX_Device* tft;
    Keypad* keypad;
    
    // Screen management
    BaseScreen* currentScreen;
    HomeScreen* homeScreen;
    NodesListScreen* nodesListScreen;
    MessagesScreen* messagesScreen;
    
    // Splash screen animation state
    bool isSplashActive;
    unsigned long splashStartTime;
    int loadingProgress;           // Current progress 0-100
    unsigned long lastProgressUpdate;  // Last time progress was updated
    class InitialSplashScreen* splashScreen; // Splash screen instance
    
    // Helper methods
    void registerInitializers();
    void connectComponents();
    void initScreens();
    void showSplashScreen();
    void updateSplashAnimation();  // Update progressive loading animation
    
    // Screen navigation
    void switchToScreen(BaseScreen* newScreen);
    
    // Input handling
    void checkKeypadInput();
    void handleKeyPress(char key);
};

// Global setup function
void setup_CustomUIModule();

// Global instance
extern CustomUIModule *customUIModule;

#endif