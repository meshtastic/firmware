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
#include <Arduino.h>
#include <SPI.h>

// Display pins for Heltec V3 with external ST7789 (Software SPI)
// Based on your wiring: MOSI=5, SCLK=6, CS=1, DC=2, RST=3, BL=4
#define TFT_MOSI 5   // Data line - GPIO5
#define TFT_SCLK 6   // Clock line - GPIO6
#define TFT_CS   1   // Chip select - GPIO1
#define TFT_DC   2   // Data/Command - GPIO2
#define TFT_RST  3   // Reset - GPIO3
#define TFT_BL   4   // Backlight - GPIO4 (optional)

CustomUIModule *customUIModule;

CustomUIModule::CustomUIModule() 
    : SinglePortModule("CustomUIModule", meshtastic_PortNum_TEXT_MESSAGE_APP),
      OSThread("CustomUIModule"),
      tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST),
      navigator(nullptr),
      lastButtonState(HIGH),
      lastButtonCheck(0),
      queueHead(0),
      queueTail(0),
      queueSize(0),
      showingMessagePopup(false),
      currentMessageIndex(-1),
      popupNeedsRedraw(false),
      lastTimestampUpdate(0) {
    
    LOG_INFO("🔧 CUSTOM UI: Module constructed with Software SPI and message queue");
}

int32_t CustomUIModule::runOnce() {
    // Check button state (USER button on Heltec V3)
    checkButtonInput();
    
    // Check for new unread messages to show
    if (!showingMessagePopup && hasUnreadMessages()) {
        showNextMessage();
    }
    
    // Update display
    if (showingMessagePopup) {
        // Only draw full popup when needed
        if (popupNeedsRedraw) {
            drawMessagePopup();
            popupNeedsRedraw = false;
            lastTimestampUpdate = millis();
        } else {
            // Just update timestamp every 10 seconds (dirty rectangle)
            unsigned long now = millis();
            if (now - lastTimestampUpdate > 10000) {
                updateTimestamp();
                lastTimestampUpdate = now;
            }
        }
        return 1000; // Normal refresh rate
    } else if (navigator) {
        navigator->update();
        // Draw message counter if there are unread messages
        if (hasUnreadMessages()) {
            drawMessageCounter();
        }
    }
    
    return 1000; // Normal refresh rate
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
    
    LOG_INFO("🔧 CUSTOM UI: ST7789 display and UI navigator initialized");
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

ProcessMessage CustomUIModule::handleReceived(const meshtastic_MeshPacket &mp) {
    // Only process text messages
    if (mp.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP && mp.decoded.payload.size > 0) {
        // Extract message text
        String messageText = String((char*)mp.decoded.payload.bytes, mp.decoded.payload.size);
        
        // Get sender info
        String senderName = "Unknown";
        uint32_t nodeId = mp.from;
        auto nodedbp = nodeDB;
        if (nodedbp) {
            meshtastic_NodeInfoLite *senderNode = nodedbp->getMeshNode(mp.from);
            if (senderNode && strlen(senderNode->user.short_name) > 0) {
                senderName = String(senderNode->user.short_name);
            } else {
                char nodeIdStr[16];
                snprintf(nodeIdStr, sizeof(nodeIdStr), "%08X", mp.from);
                senderName = String(nodeIdStr);
            }
        }
        
        // Add to message queue
        bool queued = addMessageToQueue(messageText, senderName, nodeId);
        
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
bool CustomUIModule::addMessageToQueue(const String& messageText, const String& senderName, uint32_t nodeId) {
    if (queueSize >= MAX_QUEUED_MESSAGES) {
        return false; // Queue is full
    }
    
    // Add message to queue
    QueuedMessage& msg = messageQueue[queueHead];
    msg.messageText = messageText;
    msg.senderName = senderName;
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
    if (queueSize == 0) return;
    
    currentMessageIndex = queueTail;
    showingMessagePopup = true;
    popupNeedsRedraw = true; // Flag for redraw
    
    LOG_INFO("🔧 CUSTOM UI: Showing message popup %d/%d", queueSize - (queueHead - queueTail - 1), queueSize);
}

void CustomUIModule::dismissCurrentMessage() {
    if (!showingMessagePopup || queueSize == 0) return;
    
    // Mark current message as read and remove from queue
    messageQueue[queueTail].isRead = true;
    queueTail = (queueTail + 1) % MAX_QUEUED_MESSAGES;
    queueSize--;
    
    showingMessagePopup = false;
    currentMessageIndex = -1;
    
    // Force navigator redraw when returning to normal view
    if (navigator) {
        navigator->forceRedraw();
    }
}

void CustomUIModule::drawMessagePopup() {
    if (!showingMessagePopup || currentMessageIndex < 0 || queueSize == 0) return;
    
    const QueuedMessage& msg = messageQueue[queueTail];
    
    // Draw popup overlay (semi-transparent background effect)
    tft.fillScreen(ST77XX_BLACK);
    tft.drawRect(10, 10, 300, 220, ST77XX_CYAN);
    tft.drawRect(11, 11, 298, 218, ST77XX_CYAN);
    
    // Header with message count
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(20, 20);
    tft.print("NEW MESSAGE");
    
    // Message counter
    tft.setTextSize(1);
    tft.setCursor(250, 25);
    tft.printf("(%d/%d)", queueSize - (currentMessageIndex - queueTail), queueSize);
    
    // Separator line
    tft.drawLine(20, 45, 300, 45, ST77XX_CYAN);
    
    // Sender info
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(20, 55);
    tft.print("From: ");
    tft.setTextColor(ST77XX_WHITE);
    tft.print(msg.senderName);
    
    // Node ID
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(20, 70);
    tft.print("Node: ");
    tft.setTextColor(ST77XX_WHITE);
    tft.printf("%08X", (unsigned int)msg.nodeId);
    
    // Time received (static, calculated once)
    unsigned long secondsAgo = (millis() - msg.timestamp) / 1000;
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(20, 85);
    tft.print("Time: ");
    tft.setTextColor(ST77XX_WHITE);
    if (secondsAgo < 60) {
        tft.printf("%lus ago", secondsAgo);
    } else if (secondsAgo < 3600) {
        tft.printf("%lum ago", secondsAgo / 60);
    } else {
        tft.printf("%luh ago", secondsAgo / 3600);
    }
    
    // Message separator
    tft.drawLine(20, 105, 300, 105, ST77XX_YELLOW);
    
    // Message content with word wrap
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    
    // Simple word wrap for popup (smaller area)
    String wrappedMessage = msg.messageText;
    int lineLength = 45; // Characters per line in popup
    int yPos = 120;
    int startIdx = 0;
    
    while (startIdx < wrappedMessage.length() && yPos < 190) {
        int endIdx = startIdx + lineLength;
        if (endIdx >= wrappedMessage.length()) {
            tft.setCursor(20, yPos);
            tft.println(wrappedMessage.substring(startIdx));
            break;
        }
        
        // Find last space before line limit
        int spaceIdx = wrappedMessage.lastIndexOf(' ', endIdx);
        if (spaceIdx > startIdx) {
            endIdx = spaceIdx;
        }
        
        tft.setCursor(20, yPos);
        tft.println(wrappedMessage.substring(startIdx, endIdx));
        startIdx = endIdx + 1;
        yPos += 12;
    }
    
    // Footer instructions
    tft.drawLine(20, 200, 300, 200, ST77XX_CYAN);
    tft.setTextColor(ST77XX_GREEN);
    tft.setCursor(20, 210);
    tft.print("[BUTTON] Dismiss Message");
}

void CustomUIModule::updateTimestamp() {
    if (!showingMessagePopup || currentMessageIndex < 0 || queueSize == 0) return;
    
    const QueuedMessage& msg = messageQueue[queueTail];
    
    // Clear the entire timestamp line (wider area to prevent overlap)
    tft.fillRect(70, 85, 230, 12, ST77XX_BLACK);
    
    // Redraw just the timestamp
    unsigned long secondsAgo = (millis() - msg.timestamp) / 1000;
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(70, 85);
    if (secondsAgo < 60) {
        tft.printf("%lus ago", secondsAgo);
    } else if (secondsAgo < 3600) {
        tft.printf("%lum ago", secondsAgo / 60);
    } else {
        tft.printf("%luh ago", secondsAgo / 3600);
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