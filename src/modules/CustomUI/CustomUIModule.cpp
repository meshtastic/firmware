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
#include "screens/list_screens/NodesListScreen.h"
#include "screens/list_screens/MessageListScreen.h"
#include "screens/MessageDetailsScreen.h"
#include "screens/MessagesScreen.h"
#include "screens/SnakeGameScreen.h"
#include "screens/T9InputScreen.h"
#include "InitialSplashScreen.h"
#include "screens/utils/DataStore.h"
#include "screens/utils/LoRaHelper.h"
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
      messageListScreen(nullptr),
      messageDetailsScreen(nullptr),
      snakeGameScreen(nullptr),
      t9InputScreen(nullptr),
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
    
    if (messageListScreen) {
        delete messageListScreen;
        messageListScreen = nullptr;
    }
    
    if (messageDetailsScreen) {
        delete messageDetailsScreen;
        messageDetailsScreen = nullptr;
    }
    
    if (snakeGameScreen) {
        delete snakeGameScreen;
        snakeGameScreen = nullptr;
    }
    
    if (t9InputScreen) {
        delete t9InputScreen;
        t9InputScreen = nullptr;
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

    // Create message list screen
    messageListScreen = new MessageListScreen();

    // Create message details screen
    messageDetailsScreen = new MessageDetailsScreen();

    // Create messages screen
    messagesScreen = new MessagesScreen();
    
    // Create snake game screen
    snakeGameScreen = new SnakeGameScreen();

    // Create T9 input screen
    t9InputScreen = new T9InputScreen();
    t9InputScreen->setConfirmCallback([this](const String& text) {
        this->onT9InputConfirm(text);
    });

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
        
        return 20; // Update every 20ms for smooth animation and responsive input
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
    
    return 20; // 50 FPS update rate for responsive input and smooth UI
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
        
        // Create MessageInfo and store in DataStore
        if (text.length() > 0) {
            MessageInfo messageInfo;
            
            // Copy message text (truncate if too long)
            size_t textLen = std::min((size_t)text.length(), sizeof(messageInfo.text) - 1);
            memcpy(messageInfo.text, text.c_str(), textLen);
            messageInfo.text[textLen] = '\0';
            
            // Copy sender name
            strncpy(messageInfo.senderName, sender.c_str(), sizeof(messageInfo.senderName) - 1);
            messageInfo.senderName[sizeof(messageInfo.senderName) - 1] = '\0';
            
            // Set message properties
            messageInfo.timestamp = mp.rx_time > 0 ? mp.rx_time : (millis() / 1000);
            messageInfo.senderNodeId = mp.from;
            messageInfo.toNodeId = mp.to;
            messageInfo.channelIndex = mp.channel;
            messageInfo.isOutgoing = (nodeDB && mp.from == nodeDB->getNodeNum());
            
            // Determine if this is a direct message
            messageInfo.isDirectMessage = (nodeDB && mp.to == nodeDB->getNodeNum() && mp.to != NODENUM_BROADCAST);
            
            // Format channel name
            if (messageInfo.isDirectMessage) {
                strcpy(messageInfo.channelName, "DM");
            } else if (messageInfo.channelIndex == 0) {
                strcpy(messageInfo.channelName, "Primary");
            } else {
                snprintf(messageInfo.channelName, sizeof(messageInfo.channelName), "CH%d", messageInfo.channelIndex);
            }
            
            messageInfo.isValid = true;
            
            // Store message in DataStore
            DataStore::getInstance().addMessage(messageInfo);
            
            // Show message on MessagesScreen
            if (messagesScreen) {
                unsigned long timestamp = millis();
                messagesScreen->addMessage(text, sender, timestamp);
                switchToScreen(static_cast<BaseScreen*>(messagesScreen));
            }
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
        case '1': // Select/Details for MessageListScreen, Reply for MessageDetailsScreen, or Home for others
            if (currentScreen == messageListScreen) {
                // Navigate to message details if valid selection
                if (messageListScreen->hasValidSelection()) {
                    MessageInfo selectedMsg = messageListScreen->getSelectedMessage();
                    messageDetailsScreen->setMessage(selectedMsg);
                    switchToScreen(messageDetailsScreen);
                    LOG_INFO("🔧 CUSTOM UI: Navigated to MessageDetailsScreen");
                    return;
                } else {
                    LOG_INFO("🔧 CUSTOM UI: No valid message selected");
                }
            } else if (currentScreen == messageDetailsScreen) {
                // Reply button - navigate to T9 input for reply
                if (messageDetailsScreen->hasValidMessage()) {
                    LOG_INFO("🔧 CUSTOM UI: Starting reply to message");
                    
                    // Clear any existing text in T9 input
                    t9InputScreen->clearInput();
                    
                    // Navigate to T9 input screen
                    switchToScreen(t9InputScreen);
                    return;
                } else {
                    LOG_INFO("🔧 CUSTOM UI: No valid message to reply to");
                }
            }
            // For all other screens, go to home
            if (currentScreen != homeScreen) {
                switchToScreen(homeScreen);
            }
            break;
            
        case '3': // Snake Game
            if (currentScreen != snakeGameScreen) {
                switchToScreen(snakeGameScreen);
            }
            break;

        case '7': // Nodes
            if (currentScreen != nodesListScreen) {
                switchToScreen(nodesListScreen);
            }
            break;
            
        case 'D':
        case 'd': // Message List
            if (currentScreen != messageListScreen) {
                switchToScreen(messageListScreen);
            }
            break;

        case 'A':
        case 'a': // Back/Prev/Home button
            if (currentScreen == messageDetailsScreen) {
                // Navigate back to message list screen
                switchToScreen(messageListScreen);
                LOG_INFO("🔧 CUSTOM UI: Navigated back to MessageListScreen");
            } else if (currentScreen == t9InputScreen) {
                // Navigate back to message details screen
                switchToScreen(messageDetailsScreen);
                LOG_INFO("🔧 CUSTOM UI: Navigated back to MessageDetailsScreen from T9 input");
            } else if (currentScreen == messagesScreen) {
                // If at end of buffer or no messages, go home
                if (!messagesScreen->hasMessages() || messagesScreen->handleKeyPress(key) == false) {
                    switchToScreen(homeScreen);
                }
            } else if (currentScreen == messageListScreen) {
                // Back from message list goes to home
                switchToScreen(homeScreen);
            } else if (currentScreen != homeScreen) {
                switchToScreen(homeScreen);
            }
            break;

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
    
    // Force complete screen refresh with proper state restoration
    if (currentScreen) {
        // Clear screen first
        tft->fillScreen(0x0000);
        
        // Re-enter screen to refresh data and reset state
        currentScreen->onEnter();
        
        // Force full redraw and render immediately
        currentScreen->forceRedraw();
        currentScreen->draw(*tft);
    }
    
    LOG_INFO("🔧 CUSTOM UI: Display awakened, screen state restored and refreshed");
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

// ========== Message Sending ==========
void CustomUIModule::sendReplyMessage(const String& messageText, uint32_t toNodeId, uint8_t channelIndex) {
    LOG_INFO("🔧 CUSTOM UI: Sending reply message: '%s' to node %08X on channel %d", 
             messageText.c_str(), toNodeId, channelIndex);
    
    if (messageText.length() == 0) {
        LOG_INFO("🔧 CUSTOM UI: Cannot send empty message");
        return;
    }
    
    // Use LoRaHelper to send the message
    bool success = LoRaHelper::sendMessage(messageText, toNodeId, channelIndex);
    
    if (success) {
        LOG_INFO("🔧 CUSTOM UI: ✅ Message sent successfully");
        
        // Update activity time
        updateLastActivity();
    } else {
        LOG_ERROR("🔧 CUSTOM UI: ❌ Failed to send message");
    }
}

void CustomUIModule::onT9InputConfirm(const String& text) {
    LOG_INFO("🔧 CUSTOM UI: T9 input confirmed with text: '%s'", text.c_str());
    
    // Get the current message from MessageDetailsScreen for reply context
    if (messageDetailsScreen && messageDetailsScreen->hasValidMessage()) {
        const MessageInfo& currentMsg = messageDetailsScreen->getCurrentMessage();
        
        LOG_INFO("🔧 CUSTOM UI: Sending reply to message from node %08X", currentMsg.senderNodeId);
        
        // Determine reply destination based on message type
        uint32_t replyToNode;
        uint8_t replyChannel;
        
        if (currentMsg.isDirectMessage) {
            // Reply to direct message - send back to sender as DM
            replyToNode = currentMsg.senderNodeId;
            replyChannel = 0; // DMs use primary channel
            LOG_INFO("🔧 CUSTOM UI: Replying to DM from %s", currentMsg.senderName);
        } else {
            // Reply to channel message - send to same channel
            replyToNode = UINT32_MAX; // Broadcast to channel
            replyChannel = currentMsg.channelIndex;
            LOG_INFO("🔧 CUSTOM UI: Replying to channel message on channel %d", replyChannel);
        }
        
        sendReplyMessage(text, replyToNode, replyChannel);
        
        // Navigate back to message list after sending
        if (messageListScreen) {
            switchToScreen(messageListScreen);
            LOG_INFO("🔧 CUSTOM UI: Navigated back to MessageListScreen after reply");
        }
    } else {
        LOG_ERROR("🔧 CUSTOM UI: No message context for reply");
        
        // Navigate back anyway
        if (messageListScreen) {
            switchToScreen(messageListScreen);
        }
    }
}

#endif