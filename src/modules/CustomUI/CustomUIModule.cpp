/**
 * Custom UI Module for External ST7789 Display
 * Using new navigation architecture with screen management
 * This module provides a modular UI framework for Meshtastic on ESP32-S3 boards
 * Only compiles when building the heltec-v3-custom variant
 */

#include "configuration.h"

#if defined(VARIANT_heltec_v3_custom)

#include "CustomUIModule.h"
#include "UINavigator.h"
#include "MessagePopupScreen.h"
#include <Arduino.h>
#include <SPI.h>
#include <Keypad.h>

// Display pins for Heltec V3 with external ST7789 (Software SPI)
// Based on your wiring: MOSI=5, SCLK=6, CS=1, DC=2, RST=3, BL=4
#define TFT_MOSI 5   // Data line - GPIO5
#define TFT_SCLK 6   // Clock line - GPIO6
#define TFT_CS   1   // Chip select - GPIO1
#define TFT_DC   2   // Data/Command - GPIO2
#define TFT_RST  3   // Reset - GPIO3
#define TFT_BL   4   // Backlight - GPIO4 (optional)

CustomUIModule *customUIModule;

// Static keypad configuration
char CustomUIModule::keys[CustomUIModule::ROWS][CustomUIModule::COLS] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

byte CustomUIModule::rowPins[CustomUIModule::ROWS] = {47, 33, 34, 7};
byte CustomUIModule::colPins[CustomUIModule::COLS] = {48, 21, 20, 19};

CustomUIModule::CustomUIModule() 
    : SinglePortModule("CustomUIModule", meshtastic_PortNum_TEXT_MESSAGE_APP),
      OSThread("CustomUIModule"),
      tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST),
      navigator(nullptr),
      messagePopupScreen(nullptr),
      lastButtonState(HIGH),
      lastButtonCheck(0),
      keypad(nullptr),
      queueHead(0),
      queueTail(0),
      queueSize(0),
      showingMessagePopup(false),
      currentMessageIndex(-1),
      lastTimestampUpdate(0) {
    
    LOG_INFO("🔧 CUSTOM UI: Module constructed with Software SPI and message queue");
}

int32_t CustomUIModule::runOnce() {
    // Check button state (USER button on Heltec V3)
    checkButtonInput();
    
    // Check keypad input
    checkKeypadInput();
    
    // Check for new unread messages to show
    if (!showingMessagePopup && hasUnreadMessages()) {
        showNextMessage();
    } else if (showingMessagePopup && messagePopupScreen) {
        // If popup is showing and queue size changed, update the counter
        static int lastKnownQueueSize = 0;
        if (queueSize != lastKnownQueueSize) {
            messagePopupScreen->updateCounter(tft, 1, queueSize);
            lastKnownQueueSize = queueSize;
            LOG_INFO("🔧 CUSTOM UI: Queue size changed to %d, updating counter", queueSize);
        }
    }
    
    // Update display
    if (showingMessagePopup && messagePopupScreen) {
        // Update timestamp if needed
        unsigned long now = millis();
        if (now - lastTimestampUpdate > 10000) {
            messagePopupScreen->updateTimestamp(tft, now);
            lastTimestampUpdate = now;
        }
    } else if (navigator) {
        navigator->update();
        // Draw message counter if there are unread messages
        if (hasUnreadMessages()) {
            drawMessageCounter();
        }
    }
    
    return 50; // Fast scan rate for instant keypad feedback (20Hz)
}

bool CustomUIModule::wantUIFrame() {
    return false; // We don't want to integrate with the main UI
}

void CustomUIModule::initDisplay() {
    LOG_INFO("🔧 CUSTOM UI: Initializing ST7789 display with Software SPI...");
    
    // Initialize backlight pin first
    if (TFT_BL >= 0) {
        pinMode(TFT_BL, OUTPUT);
        digitalWrite(TFT_BL, HIGH); // Turn on backlight
        delay(100);
    }
    
    // Initialize the display (no SPI.begin needed for software SPI)
    tft.init(240, 320);
    delay(100);
    tft.setRotation(1); // Landscape mode: 320x240
    tft.fillScreen(ST77XX_BLACK);
    
    // Show custom Hacker Central initialization message
    LOG_INFO("🔧 CUSTOM UI: Testing display functionality...");
    
    // Show initialization message
    tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
    tft.setTextSize(3);
    // Center "HACKER CENTRAL" horizontally
    tft.setCursor(40, 90);
    tft.println("HACKER CENTRAL");
    
    // "Initializing..." in bottom right corner
    tft.setTextSize(1);
    tft.setCursor(200, 200);
    tft.println("Initializing...");
    
    delay(2000); // Show title for 2 seconds
    
    // Create UI navigator
    navigator = new UINavigator(tft);
    
    // Create message popup screen
    messagePopupScreen = new MessagePopupScreen(navigator);
    
    // Initialize keypad
    keypad = new Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
    keypad->setDebounceTime(110); // Set debounce time as specified
    
    LOG_INFO("🔧 CUSTOM UI: ST7789 display, UI navigator, message popup, and keypad initialized");
}

void CustomUIModule::checkButtonInput() {
    unsigned long currentTime = millis();
    
    // Debounce button (check every 50ms)
    if (currentTime - lastButtonCheck < 50) {
        return;
    }
    lastButtonCheck = currentTime;
    
    // Read button state (USER button on pin 0 for Heltec V3)
    int buttonState = digitalRead(0);
    
    // Check for button press (LOW = pressed, with pullup)
    if (buttonState == LOW && lastButtonState == HIGH) {
        LOG_INFO("🔧 CUSTOM UI: User button pressed");
        
        if (showingMessagePopup) {
            // If showing message popup, button press dismisses it
            dismissCurrentMessage();
            LOG_INFO("🔧 CUSTOM UI: Message popup dismissed by user");
        } else {
            // Normal button handling through navigator
            if (navigator) {
                navigator->handleInput(1); // Button input = 1
            }
        }
    }
    
    lastButtonState = buttonState;
}

void CustomUIModule::checkKeypadInput() {
    if (!keypad) return;
    
    char key = keypad->getKey();
    
    if (key) {
        LOG_INFO("🔧 CUSTOM UI: Keypad key pressed: %c", key);
        
        if (showingMessagePopup) {
            // Any key dismisses message popup
            dismissCurrentMessage();
            LOG_INFO("🔧 CUSTOM UI: Message popup dismissed by keypad");
        } else {
            // Handle specific keys
            switch (key) {
                case 'A':
                    // Back navigation
                    if (navigator) {
                        LOG_INFO("🔧 CUSTOM UI: Keypad 'A' pressed - navigating back");
                        navigator->navigateBack();
                    }
                    break;
                    
                case '1':
                    // Open nodes list
                    if (navigator) {
                        LOG_INFO("🔧 CUSTOM UI: Keypad '1' pressed - opening nodes list");
                        navigator->navigateToNodes();
                    }
                    break;
                    
                default:
                    LOG_DEBUG("🔧 CUSTOM UI: Unhandled keypad key: %c", key);
                    break;
            }
        }
    }
}

ProcessMessage CustomUIModule::handleReceived(const meshtastic_MeshPacket &mp) {
    // Only process text messages
    if (mp.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP && mp.decoded.payload.size > 0) {
        // Extract message text
        String messageText = String((char*)mp.decoded.payload.bytes, mp.decoded.payload.size);
        
        // Get sender info
        String senderName = "Unknown";
        String senderLongName = "";
        uint32_t nodeId = mp.from;
        auto nodedbp = nodeDB;
        if (nodedbp) {
            meshtastic_NodeInfoLite *senderNode = nodedbp->getMeshNode(mp.from);
            if (senderNode) {
                if (strlen(senderNode->user.short_name) > 0) {
                    senderName = String(senderNode->user.short_name);
                }
                if (strlen(senderNode->user.long_name) > 0) {
                    senderLongName = String(senderNode->user.long_name);
                }
                if (senderName == "Unknown") {
                    char nodeIdStr[16];
                    snprintf(nodeIdStr, sizeof(nodeIdStr), "%08X", mp.from);
                    senderName = String(nodeIdStr);
                }
            } else {
                char nodeIdStr[16];
                snprintf(nodeIdStr, sizeof(nodeIdStr), "%08X", mp.from);
                senderName = String(nodeIdStr);
            }
        }
        
        // Add to message queue
        bool queued = addMessageToQueue(messageText, senderName, senderLongName, nodeId);
        
        if (queued) {
            LOG_INFO("🔧 CUSTOM UI: Message queued from %s: %s", senderName.c_str(), messageText.c_str());
        } else {
            LOG_WARN("🔧 CUSTOM UI: Message queue full, dropping message from %s", senderName.c_str());
        }
    }
    
    return ProcessMessage::CONTINUE; // Let other modules process too
}

void setup_CustomUIModule() {
    if (!customUIModule) {
        customUIModule = new CustomUIModule();
        customUIModule->initDisplay();
    }
}

// Message queue management methods
bool CustomUIModule::addMessageToQueue(const String& messageText, const String& senderName, const String& senderLongName, uint32_t nodeId) {
    if (queueSize >= MAX_QUEUED_MESSAGES) {
        return false; // Queue is full
    }
    
    // Add message to queue
    QueuedMessage& msg = messageQueue[queueHead];
    msg.messageText = messageText;
    msg.senderName = senderName;
    msg.senderLongName = senderLongName;
    msg.nodeId = nodeId;
    msg.timestamp = millis();
    msg.isRead = false;
    
    // Update queue pointers
    queueHead = (queueHead + 1) % MAX_QUEUED_MESSAGES;
    queueSize++;
    
    return true;
}

bool CustomUIModule::hasUnreadMessages() const {
    return queueSize > 0;
}

void CustomUIModule::showNextMessage() {
    if (queueSize == 0 || !messagePopupScreen) return;
    
    currentMessageIndex = queueTail;
    const QueuedMessage& msg = messageQueue[queueTail];
    
    // Prepare message data for popup
    MessageData msgData;
    msgData.messageText = msg.messageText;
    msgData.senderName = msg.senderName;
    msgData.senderLongName = msg.senderLongName;
    msgData.nodeId = msg.nodeId;
    msgData.timestamp = msg.timestamp;
    // Calculate actual position: if we have 3 messages, first is 1/3, next is 2/3, last is 3/3
    // Position = total messages - remaining messages + 1
    msgData.currentIndex = 1;  // Always showing the "next" message (first in queue)
    msgData.totalMessages = queueSize;
    
    messagePopupScreen->showMessage(msgData);
    messagePopupScreen->draw(tft, navigator->getDataState());
    showingMessagePopup = true;
    lastTimestampUpdate = millis();
    
    LOG_INFO("🔧 CUSTOM UI: Showing message popup %d/%d", msgData.currentIndex, msgData.totalMessages);
}

void CustomUIModule::dismissCurrentMessage() {
    if (!showingMessagePopup || queueSize == 0) return;
    
    // Mark current message as read and remove from queue
    messageQueue[queueTail].isRead = true;
    queueTail = (queueTail + 1) % MAX_QUEUED_MESSAGES;
    queueSize--;
    
    // If there are more messages, show the next one
    if (queueSize > 0) {
        LOG_INFO("🔧 CUSTOM UI: %d more messages in queue, showing next", queueSize);
        showNextMessage();
    } else {
        // No more messages, return to normal view
        showingMessagePopup = false;
        currentMessageIndex = -1;
        
        // Force navigator redraw when returning to normal view
        if (navigator) {
            navigator->forceRedraw();
        }
        LOG_INFO("🔧 CUSTOM UI: All messages dismissed, returning to home");
    }
}

void CustomUIModule::drawMessageCounter() {
    if (queueSize == 0) return;
    
    // Draw small message indicator in corner
    tft.fillRect(290, 10, 25, 15, ST77XX_RED);
    tft.drawRect(289, 9, 27, 17, ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(295, 12);
    if (queueSize > 9) {
        tft.print("9+");
    } else {
        tft.print(queueSize);
    }
}

#endif