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
#include "screens/NodesListScreen.h"
#include "screens/MessagesScreen.h"
#include "InitialSplashScreen.h"
#include "sleep.h"
#include <LovyanGFX.hpp>
#include <Arduino.h>

#ifdef ESP32
#include <esp_heap_caps.h>
#include <esp_heap_caps_init.h>
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
      nodesListScreen(nullptr),
      isSplashActive(false),
      splashStartTime(0),
      loadingProgress(0),
      lastProgressUpdate(0),
      splashScreen(nullptr),
      displayAsleep(false),
      lastActivityTime(0) {
    
    LOG_INFO("🔧 CUSTOM UI: Module constructed with screen-based architecture");
    registerInitializers();
    
    // Register for deep sleep notifications to ensure proper cleanup
    deepSleepObserver.observe(&notifyDeepSleep);
    LOG_INFO("🔧 CUSTOM UI: Registered deep sleep observer");
}

CustomUIModule::~CustomUIModule() {
    // Unregister deep sleep observer
    deepSleepObserver.unobserve(&notifyDeepSleep);
    
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
        
        // Set initial activity time
        updateLastActivity();
        
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
    
    // Handle keypad input first (needed to wake display)
    checkKeypadInput();
    
    // Check for display sleep timeout
    checkDisplaySleep();
    
    // Skip UI updates if display is asleep
    // if (displayAsleep) {
    //     return 1000; // Check for wake conditions every second
    // }
    
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
        // Wake display if asleep
        if (displayAsleep) {
            wakeDisplay();
        }
        
        // Update activity time
        updateLastActivity();
        
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
    
    // Exit current screen
    if (currentScreen) {
        currentScreen->onExit();
    }
    
    // Force display buffer clearing
    if (tft) {
        tft->waitDisplay();
    }
    
    // Memory cleanup
#ifdef ESP32
    heap_caps_check_integrity_all(true);
#endif
    delay(10);
    
    // Switch to new screen
    currentScreen = newScreen;
    currentScreen->onEnter();
    
    // Force full redraw
    if (tft) {
        tft->fillScreen(0x0000);
    }
}

// ========== Input Handling Methods ==========
void CustomUIModule::checkKeypadInput() {
    if (!keypad) return;
    
    char key = keypad->getKey();
    
    if (key) {
        LOG_INFO("🔧 CUSTOM UI: Keypad key pressed: %c (display asleep: %s)", key, displayAsleep ? "YES" : "NO");
        
        // Wake display if asleep
        if (displayAsleep) {
            wakeDisplay();
            return; // First keypress just wakes display
        }
        
        // Update activity time and handle key
        updateLastActivity();
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
                switchToScreen(homeScreen);
            }
            break;

        case '7': // Nodes
            if (currentScreen != nodesListScreen) {
                switchToScreen(nodesListScreen);
            }
            break;

        case 'A':
        case 'a': // Back/Prev/Home button
            if (currentScreen == messagesScreen) {
                // If at end of buffer or no messages, go home
                if (!messagesScreen->hasMessages() || messagesScreen->handleKeyPress(key) == false) {
                    switchToScreen(homeScreen);
                }
            } else if (currentScreen != homeScreen) {
                switchToScreen(homeScreen);
            }
            break;

        case '*': {
            // Simple memory cleanup
            ESP.getMinFreeHeap();
            delay(50);
            break;
        }

        case '#': {
            // Basic memory status
            size_t totalHeap = ESP.getHeapSize();
            size_t freeHeap = ESP.getFreeHeap();
            LOG_INFO("🔧 Memory: %.1f%% used, %zu KB free", 
                (float)(totalHeap - freeHeap) * 100.0 / totalHeap, freeHeap/1024);
            break;
        }

        default:
            break;
    }
}

// ========== Display Power Management ==========
void CustomUIModule::checkDisplaySleep() {
    if (displayAsleep || !tft) {
        return;
    }
    
    unsigned long currentTime = millis();
    unsigned long timeSinceActivity = currentTime - lastActivityTime;
    
    // Check if timeout exceeded
    if (timeSinceActivity >= DISPLAY_SLEEP_TIMEOUT) {
        LOG_INFO("🔧 CUSTOM UI: Display sleep timeout reached (%lu ms since last activity)", timeSinceActivity);
        sleepDisplay();
    }
}

void CustomUIModule::sleepDisplay() {
    if (displayAsleep || !tft) {
        return;
    }
    
    LOG_INFO("🔧 CUSTOM UI: Putting display to sleep after inactivity");
    
    // Turn off display using LovyanGFX sleep function
    tft->sleep();
    displayAsleep = true;
}

void CustomUIModule::wakeDisplay() {
    if (!displayAsleep || !tft) {
        return;
    }
    
    LOG_INFO("🔧 CUSTOM UI: Waking display from activity");
    
    // Wake up display using LovyanGFX wakeup function
    tft->wakeup();
    
    // Give display time to stabilize
    delay(50);
    
    displayAsleep = false;
    
    // Update activity time
    updateLastActivity();
    
    // Force complete screen refresh
    if (currentScreen) {
        // Clear screen first
        tft->fillScreen(0x0000);
        
        // Force full redraw
        currentScreen->forceRedraw();
        currentScreen->draw(*tft);
    }
    
    LOG_INFO("🔧 CUSTOM UI: Display awakened and screen refreshed");
}

void CustomUIModule::updateLastActivity() {
    lastActivityTime = millis();
}

// ========== Deep Sleep Cleanup ==========
int CustomUIModule::onDeepSleep(void *unused) {
    LOG_INFO("🔧 CUSTOM UI: Preparing for deep sleep - cleaning up display");
    
    if(displayAsleep){
        wakeDisplay();
    }

    // Force any pending display operations to complete
    if (tft) {
        tft->waitDisplay();
        
        // Show a shutdown message briefly
        tft->fillScreen(0x0000);
        tft->setTextColor(0xFFFF); // White text
        tft->setTextSize(2);
        tft->setCursor(80, 110);
        tft->print("Sleeping...");
        delay(500); // Show message briefly
        
        // Put display into sleep mode
        tft->sleep();
        LOG_INFO("🔧 CUSTOM UI: Display put to sleep");
    }
    
    // Mark display as asleep
    displayAsleep = true;
    
    // Cleanup all initializers properly
    for (auto& init : initializers) {
        if (init) {
            init->cleanup();
            LOG_INFO("🔧 CUSTOM UI: Cleaned up %s", init->getName());
        }
    }
    
    LOG_INFO("🔧 CUSTOM UI: Deep sleep cleanup completed");
    return 0; // Allow deep sleep to proceed
}

#endif