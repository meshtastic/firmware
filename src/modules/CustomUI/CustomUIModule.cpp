/**
 * Modular Custom UI Module for External ST7789 Display with LovyanGFX
 * Uses modular architecture with separate initializers and screen-based UI
 * Only compiles when building the heltec-v3-custom variant
 */

#include "configuration.h"

#if defined(VARIANT_heltec_v3_custom)

#include "CustomUIModule.h"
#include "init/InitBase.h"
#include "init/InitDisplay.h"
#include "init/InitKeypad.h"
#include "screens/BaseScreen.h"
#include "screens/HomeScreen.h"
#include "screens/WiFiListScreen.h"
#include "screens/NodesListScreen.h"
#include "InitialSplashScreen.h"
#include <LovyanGFX.hpp>
#include <Arduino.h>

CustomUIModule *customUIModule;

CustomUIModule::CustomUIModule() 
    : SinglePortModule("CustomUIModule", meshtastic_PortNum_TEXT_MESSAGE_APP),
      OSThread("CustomUIModule"),
      displayInit(nullptr),
      keypadInit(nullptr),
      allInitialized(false),
      tft(nullptr),
      keypad(nullptr),
      currentScreen(nullptr),
      homeScreen(nullptr),
      wifiListScreen(nullptr),
      nodesListScreen(nullptr),
      isSplashActive(false),
      splashStartTime(0) {
    
    LOG_INFO("🔧 CUSTOM UI: Module constructed with screen-based architecture");
    registerInitializers();
}

CustomUIModule::~CustomUIModule() {
    // Cleanup screens
    if (homeScreen) {
        delete homeScreen;
        homeScreen = nullptr;
    }
    
    if (wifiListScreen) {
        delete wifiListScreen;
        wifiListScreen = nullptr;
    }
    
    if (nodesListScreen) {
        delete nodesListScreen;
        nodesListScreen = nullptr;
    }
    
    // Cleanup all initializers
    for (auto& init : initializers) {
        init->cleanup();
    }
    initializers.clear();
}

void CustomUIModule::registerInitializers() {
    LOG_INFO("🔧 CUSTOM UI: Registering initializers...");
    
    // Register display initializer
    std::unique_ptr<InitDisplay> display(new InitDisplay());
    displayInit = display.get(); // Keep reference for easy access
    initializers.push_back(std::move(display));
    
    // Register keypad initializer
    std::unique_ptr<InitKeypad> keypadInitPtr(new InitKeypad());
    keypadInit = keypadInitPtr.get(); // Keep reference for easy access
    initializers.push_back(std::move(keypadInitPtr));
    
    // Future initializers can be added here:
    // initializers.push_back(std::unique_ptr<InitWiFi>(new InitWiFi()));
    // initializers.push_back(std::unique_ptr<InitBluetooth>(new InitBluetooth()));
    
    LOG_INFO("🔧 CUSTOM UI: Registered %d initializers", initializers.size());
}

void CustomUIModule::initAll() {
    LOG_INFO("🔧 CUSTOM UI: Starting initialization sequence...");
    
    bool allSuccess = true;
    
    // Initialize all components in order
    for (auto& init : initializers) {
        LOG_INFO("🔧 CUSTOM UI: Initializing %s...", init->getName());
        
        if (!init->init()) {
            LOG_ERROR("🔧 CUSTOM UI: Failed to initialize %s", init->getName());
            allSuccess = false;
        } else {
            LOG_INFO("🔧 CUSTOM UI: ✅ %s initialized successfully", init->getName());
        }
    }
    
    if (allSuccess) {
        // Connect components after all are initialized
        connectComponents();
        
        // Initialize screens
        initScreens();
        
        allInitialized = true;
        LOG_INFO("🔧 CUSTOM UI: ✅ All initializers and screens completed successfully");
    } else {
        LOG_ERROR("🔧 CUSTOM UI: ❌ Some initializers failed");
    }
}

void CustomUIModule::connectComponents() {
    LOG_INFO("🔧 CUSTOM UI: Connecting components...");
    
    // Get direct access to initialized components for logic handling
    if (displayInit && displayInit->isReady()) {
        tft = displayInit->getDisplay();
        LOG_INFO("🔧 CUSTOM UI: Display connected");
        
        // Show splash screen immediately after display is ready
        showSplashScreen();
        
        // Initialize splash animation state
        isSplashActive = true;
        splashStartTime = millis();
        
        // Keep splash screen active - don't switch to home yet
        // We'll switch to home screen later in runOnce() when everything is ready
    }
    
    if (keypadInit && keypadInit->isReady()) {
        keypad = keypadInit->getKeypad();
        LOG_INFO("🔧 CUSTOM UI: Keypad connected");
    }
}

void CustomUIModule::showSplashScreen() {
    if (!tft) return;
    
    LOG_INFO("🔧 CUSTOM UI: Starting animated splash screen");
    
    // Create and play the animated splash screen
    InitialSplashScreen splashScreen;
    splashScreen.playAnimation(tft);
    
    LOG_INFO("🔧 CUSTOM UI: Animated splash screen completed");
}

void CustomUIModule::initScreens() {
    LOG_INFO("🔧 CUSTOM UI: Initializing screens...");
    
    // Create home screen
    homeScreen = new HomeScreen();
    
    // Create WiFi list screen
    wifiListScreen = new WiFiListScreen();
    
    // Create nodes list screen
    nodesListScreen = new NodesListScreen();
    
    // DON'T switch to home screen yet - keep splash screen active
    // We'll switch when everything is fully initialized
    
    LOG_INFO("🔧 CUSTOM UI: ✅ Screens created, splash screen still active");
}

int32_t CustomUIModule::runOnce() {
    if (!allInitialized) {
        return 1000; // Wait 1 second if not initialized
    }
    
    // Handle splash screen - just wait for initialization to complete
    if (isSplashActive && tft) {
        // Stop splash screen when all components are properly initialized and screens are ready
        if (allInitialized && homeScreen && wifiListScreen && nodesListScreen) {
            LOG_INFO("🔧 CUSTOM UI: All components initialized, transitioning to Home screen");
            isSplashActive = false;
            switchToScreen(homeScreen);
        }
        
        return 100; // Simple check every 100ms
    }
    
    if (!currentScreen || !tft) {
        return 1000; // Wait 1 second if no screen ready
    }
    
    // Handle keypad input
    checkKeypadInput();
    
    // Update current screen if needed
    if (currentScreen->needsUpdate()) {
        currentScreen->draw(*tft);
    }
    
    return 50; // 20 FPS update rate for responsive UI
}

bool CustomUIModule::wantUIFrame() {
    return false; // We don't want to integrate with the main UI
}

ProcessMessage CustomUIModule::handleReceived(const meshtastic_MeshPacket &mp) {
    // Process incoming messages if needed
    // Future: could trigger screen updates for new messages
    return ProcessMessage::CONTINUE;
}

void setup_CustomUIModule() {
    if (!customUIModule) {
        customUIModule = new CustomUIModule();
        customUIModule->initAll();
    }
}

// ========== Screen Navigation ==========
void CustomUIModule::switchToScreen(BaseScreen* newScreen) {
    if (!newScreen || newScreen == currentScreen) {
        return;
    }
    
    LOG_INFO("🔧 CUSTOM UI: Switching to screen: %s", newScreen->getName().c_str());
    
    // Exit current screen
    if (currentScreen) {
        currentScreen->onExit();
    }
    
    // Switch to new screen
    currentScreen = newScreen;
    currentScreen->onEnter();
    
    // Force full redraw with black background
    if (tft) {
        tft->fillScreen(0x0000); // Pure black background for testing
        LOG_INFO("🔧 CUSTOM UI: Switched to black screen");
    }
}

// ========== Input Handling Methods ==========
void CustomUIModule::checkKeypadInput() {
    if (!keypad) return;
    
    char key = keypad->getKey();
    
    if (key) {
        LOG_INFO("🔧 CUSTOM UI: Keypad key pressed: %c", key);
        handleKeyPress(key);
    }
}

void CustomUIModule::handleKeyPress(char key) {
    if (!currentScreen) return;
    
    // Let current screen handle the key first
    if (currentScreen->handleKeyPress(key)) {
        return; // Screen handled the key
    }
    
    // Handle global navigation keys
    switch (key) {
        case '1': // Home
            if (currentScreen != homeScreen) {
                LOG_INFO("🔧 CUSTOM UI: Navigating to Home screen");
                switchToScreen(homeScreen);
            }
            break;
            
        case '3': // WiFi
            if (currentScreen != wifiListScreen) {
                LOG_INFO("🔧 CUSTOM UI: Navigating to WiFi screen");
                switchToScreen(wifiListScreen);
            }
            break;
            
        case '7': // Nodes
            if (currentScreen != nodesListScreen) {
                LOG_INFO("🔧 CUSTOM UI: Navigating to Nodes screen");
                switchToScreen(nodesListScreen);
            }
            break;
            
        case 'A':
        case 'a': // Back button
            if (currentScreen != homeScreen) {
                LOG_INFO("🔧 CUSTOM UI: Back button - returning to Home");
                switchToScreen(homeScreen);
            }
            break;
            
        case '*':
            LOG_INFO("🔧 CUSTOM UI: Global clear/back key pressed");
            // Could be used for back navigation in future
            break;
            
        case '#':
            LOG_INFO("🔧 CUSTOM UI: Global menu/enter key pressed");
            // Could be used for menu in future
            break;
            
        default:
            LOG_INFO("🔧 CUSTOM UI: Unhandled key: %c", key);
            break;
    }
}

#endif