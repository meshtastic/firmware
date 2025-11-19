/**
 * Modular Custom UI Module for External ST7789 Display with LovyanGFX
 * Uses modular architecture with separate initializers and screen-based UI
 * Only compiles when building the heltec-v3-custom variant
 */

#include "configuration.h"

#if defined(VARIANT_heltec_v3_custom)

#include "CustomUIModule.h"
#include "DebugConfiguration.h"
#include "init/InitBase.h"
#include "init/InitDisplay.h"
#include "init/InitKeypad.h"
#include "screens/BaseScreen.h"
#include "screens/HomeScreen.h"
#include "screens/WiFiListScreen.h"
#include "screens/NodesListScreen.h"
#include "screens/MessagesScreen.h"
#include "InitialSplashScreen.h"
#include <LovyanGFX.hpp>
#include <Arduino.h>

#ifdef ESP32
#include <esp_heap_caps.h>
#endif

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
      splashStartTime(0),
      loadingProgress(0),
      lastProgressUpdate(0),
      splashScreen(nullptr) {
    
    LOG_INFO("🔧 CUSTOM UI: Module constructed with screen-based architecture");
    registerInitializers();
}

CustomUIModule::~CustomUIModule() {
    // Cleanup splash screen
    if (splashScreen) {
        delete splashScreen;
        splashScreen = nullptr;
    }
    
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
        
        // Report current memory status with PSRAM info
        LOG_INFO("🔧 CUSTOM UI: Post-display Memory Status:");
        LOG_INFO("🔧 CUSTOM UI: - Free Heap: %zu bytes (%.1fKB)", ESP.getFreeHeap(), ESP.getFreeHeap()/1024.0);
        
#if defined(CONFIG_SPIRAM_SUPPORT) && defined(BOARD_HAS_PSRAM)
        size_t psramSize = ESP.getPsramSize();
        if (psramSize > 0) {
            size_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
            LOG_INFO("🔧 CUSTOM UI: - PSRAM Total: %zu bytes (%.1fMB)", psramSize, psramSize/(1024.0*1024.0));
            LOG_INFO("🔧 CUSTOM UI: - PSRAM Free: %zu bytes (%.1fMB)", freePsram, freePsram/(1024.0*1024.0));
            LOG_INFO("🔧 CUSTOM UI: ✅ PSRAM available for graphics");
        } else {
            LOG_INFO("🔧 CUSTOM UI: ⚠️  No PSRAM detected");
        }
#else
        LOG_INFO("🔧 CUSTOM UI: ⚠️  PSRAM support not compiled in");
#endif
        
        // Show splash screen with progressive animation
        showSplashScreen();
    }
    
    if (keypadInit && keypadInit->isReady()) {
        keypad = keypadInit->getKeypad();
        LOG_INFO("🔧 CUSTOM UI: Keypad connected");
    }
}

void CustomUIModule::showSplashScreen() {
    if (!tft) return;
    
    LOG_INFO("🔧 CUSTOM UI: Starting progressive loading animation");
    
    // Create splash screen instance
    splashScreen = new InitialSplashScreen();
    
    // Initialize the splash screen (title and progress bar setup)
    splashScreen->playAnimation(tft);
    
    // Initialize animation state
    loadingProgress = 0;
    lastProgressUpdate = millis();
    isSplashActive = true;
    
    LOG_INFO("🔧 CUSTOM UI: Progressive loading animation initialized");
}

void CustomUIModule::initScreens() {
    LOG_INFO("🔧 CUSTOM UI: Initializing screens...");
    
    // Create home screen
    homeScreen = new HomeScreen();

    // Create WiFi list screen
    wifiListScreen = new WiFiListScreen();

    // Create nodes list screen
    nodesListScreen = new NodesListScreen();

    // Create messages screen
    messagesScreen = new MessagesScreen();

    // Screens are ready but don't switch yet - animation will handle transition

    LOG_INFO("🔧 CUSTOM UI: ✅ Screens created, animation will handle transition");
}

int32_t CustomUIModule::runOnce() {
    if (!allInitialized) {
        return 1000; // Wait 1 second if not initialized
    }
    
    // Handle progressive splash screen animation
    if (isSplashActive && tft && splashScreen) {
        updateSplashAnimation();
        
        // Check if animation is complete
        if (splashScreen->isAnimationComplete()) {
            LOG_INFO("🔧 CUSTOM UI: Animation complete, transitioning to Home screen");
            isSplashActive = false;
            
            // Clean up splash screen
            delete splashScreen;
            splashScreen = nullptr;
            
            // Switch to home screen
            if (homeScreen) {
                switchToScreen(homeScreen);
            }
        }
        
        return 50; // Update every 50ms for smooth animation
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


// Handle incoming LoRa messages and show MessagesScreen
ProcessMessage CustomUIModule::handleReceived(const meshtastic_MeshPacket &mp) {
    // Only handle text messages (TEXT_MESSAGE_APP)
    if (mp.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
        // Extract text from payload (payload.bytes is not null-terminated)
        const meshtastic_Data_payload_t &payload = mp.decoded.payload;
        String text;
        if (payload.size > 0 && payload.bytes != nullptr) {
            text = String(reinterpret_cast<const char *>(payload.bytes), payload.size);
        }
        // Try to get sender long name from NodeDB
        String sender;
        if (nodeDB) {
            meshtastic_NodeInfoLite *info = nodeDB->getMeshNode(mp.from);
            if (info && info->user.long_name[0] != '\0') {
                sender = String(info->user.long_name);
            } else {
                char senderBuf[12];
                snprintf(senderBuf, sizeof(senderBuf), "%08X", mp.from);
                sender = String(senderBuf);
            }
        } else {
            char senderBuf[12];
            snprintf(senderBuf, sizeof(senderBuf), "%08X", mp.from);
            sender = String(senderBuf);
        }
        unsigned long timestamp = millis();
        if (messagesScreen && text.length() > 0) {
            messagesScreen->addMessage(text, sender, timestamp);
            switchToScreen(static_cast<BaseScreen*>(messagesScreen));
        }
    }
    return ProcessMessage::CONTINUE;
}

void setup_CustomUIModule() {
    if (!customUIModule) {
        customUIModule = new CustomUIModule();
        customUIModule->initAll();
    }
}

void CustomUIModule::updateSplashAnimation() {
    if (!splashScreen || !tft) {
        return;
    }
    
    unsigned long currentTime = millis();
    
    // Update progress every 30ms for smooth animation (about 33 FPS)
    if (currentTime - lastProgressUpdate >= 30) {
        loadingProgress += 2; // Increment by 2% each update
        
        // Ensure we don't exceed 100%
        if (loadingProgress > 100) {
            loadingProgress = 100;
        }
        
        // Update the splash screen with current progress
        splashScreen->updateLoadingProgress(tft, loadingProgress);
        
        lastProgressUpdate = currentTime;
        
        // Log progress for debugging (every 20%)
        if (loadingProgress % 20 == 0) {
            LOG_INFO("🔧 CUSTOM UI: Loading progress: %d%%", loadingProgress);
        }
    }
}

// ========== Screen Navigation ==========
void CustomUIModule::switchToScreen(BaseScreen* newScreen) {
    if (!newScreen || newScreen == currentScreen) {
        return;
    }
    
    LOG_INFO("🔧 CUSTOM UI: Switching to screen: %s", newScreen->getName().c_str());
    
    // Report memory before switch
    // size_t freeHeapBefore = ESP.getFreeHeap();
    
    // Exit current screen
    if (currentScreen) {
        currentScreen->onExit();
    }
    
    // Force garbage collection and memory cleanup
    if (tft) {
        tft->waitDisplay(); // Wait for any pending display operations
    }
    
    // Switch to new screen
    currentScreen = newScreen;
    currentScreen->onEnter();
    
    // Force full redraw with black background
    if (tft) {
        tft->fillScreen(0x0000); // Pure black background
        LOG_INFO("🔧 CUSTOM UI: Screen switched, memory freed");
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
        case 'a': // Back/Prev/Home button
            if (currentScreen == messagesScreen) {
                // If at end of buffer or no messages, go home
                if (!messagesScreen->hasMessages() || messagesScreen->handleKeyPress(key) == false) {
                    LOG_INFO("🔧 CUSTOM UI: MessagesScreen [A] - returning to Home");
                    switchToScreen(homeScreen);
                }
            } else if (currentScreen != homeScreen) {
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
            // Memory status report on demand
            LOG_INFO("🔧 CUSTOM UI: === MEMORY STATUS REPORT ===");
            LOG_INFO("🔧 CUSTOM UI: Free Heap: %zu bytes (%.1fKB)", ESP.getFreeHeap(), ESP.getFreeHeap()/1024.0);
            LOG_INFO("🔧 CUSTOM UI: Total Heap: %zu bytes (%.1fKB)", ESP.getHeapSize(), ESP.getHeapSize()/1024.0);
            LOG_INFO("🔧 CUSTOM UI: Min Free Heap: %zu bytes (%.1fKB)", ESP.getMinFreeHeap(), ESP.getMinFreeHeap()/1024.0);
            LOG_INFO("🔧 CUSTOM UI: Memory Usage: %.1f%%", (float)(ESP.getHeapSize() - ESP.getFreeHeap()) * 100.0 / ESP.getHeapSize());
            LOG_INFO("🔧 CUSTOM UI: Hardware: Heltec V3 (SRAM only, no PSRAM)");
            break;

        default:
            LOG_INFO("🔧 CUSTOM UI: Unhandled key: %c", key);
            break;
    }
}

#endif