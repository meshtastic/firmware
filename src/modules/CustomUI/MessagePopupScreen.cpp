#include "MessagePopupScreen.h"
#include "UINavigator.h"
#include "configuration.h"
#include <Arduino.h>

MessagePopupScreen::MessagePopupScreen(UINavigator* navigator)
    : BaseScreen(navigator, "MESSAGE_POPUP"), hasMessage(false), lastTimestampUpdate(0),
      lastDrawnTimestamp(0), lastDrawnCurrentIndex(0), lastDrawnTotalMessages(0) {
}

void MessagePopupScreen::onEnter() {
    LOG_INFO("🔧 UI: Entering Message Popup Screen");
    markForFullRedraw();
    lastTimestampUpdate = millis();
}

void MessagePopupScreen::onExit() {
    LOG_INFO("🔧 UI: Exiting Message Popup Screen");
    hasMessage = false;
    lastDrawnTimestamp = 0;
    lastDrawnCurrentIndex = 0;
    lastDrawnTotalMessages = 0;
}

void MessagePopupScreen::handleInput(uint8_t input) {
    // Any input dismisses the popup
    LOG_INFO("🔧 UI: Message popup dismissed by input");
    // Navigation back is handled by CustomUIModule
}

bool MessagePopupScreen::needsUpdate(UIDataState& dataState) {
    // Only update timestamp every 10 seconds
    unsigned long now = millis();
    return (now - lastTimestampUpdate > 10000);
}

void MessagePopupScreen::draw(Adafruit_ST7789& tft, UIDataState& dataState) {
    if (!hasMessage) return;
    
    drawMessageContent(tft);
}

void MessagePopupScreen::showMessage(const MessageData& msg) {
    currentMessage = msg;
    hasMessage = true;
    // Reset tracking so counter will be redrawn
    lastDrawnCurrentIndex = 0;
    lastDrawnTotalMessages = 0;
    lastDrawnTimestamp = 0;
    markForFullRedraw();
}

void MessagePopupScreen::drawMessageContent(Adafruit_ST7789& tft) {
    if (!hasMessage) return;
    
    // Draw popup overlay (semi-transparent background effect)
    tft.fillScreen(ST77XX_BLACK);
    tft.drawRect(10, 10, 300, 220, ST77XX_CYAN);
    tft.drawRect(11, 11, 298, 218, ST77XX_CYAN);
    
    // Header with message count
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(20, 20);
    tft.print("NEW MESSAGE");
    
    // Message counter - only update if changed
    if (currentMessage.currentIndex != lastDrawnCurrentIndex || 
        currentMessage.totalMessages != lastDrawnTotalMessages) {
        tft.setTextSize(1);
        // Clear old counter area
        tft.fillRect(240, 20, 70, 20, ST77XX_BLACK);
        tft.setTextColor(ST77XX_CYAN);
        tft.setCursor(250, 25);
        tft.printf("(%d/%d)", currentMessage.currentIndex, currentMessage.totalMessages);
        lastDrawnCurrentIndex = currentMessage.currentIndex;
        lastDrawnTotalMessages = currentMessage.totalMessages;
    }
    
    // Separator line
    tft.drawLine(20, 45, 300, 45, ST77XX_CYAN);
    
    // Sender info
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(20, 55);
    tft.print("From: ");
    tft.setTextColor(ST77XX_WHITE);
    tft.print(currentMessage.senderName);
    
    // Sender long name (if different from short name)
    if (currentMessage.senderLongName.length() > 0 && 
        currentMessage.senderLongName != currentMessage.senderName) {
        tft.setTextColor(ST77XX_YELLOW);
        tft.setCursor(20, 70);
        tft.print("Name: ");
        tft.setTextColor(ST77XX_WHITE);
        tft.print(currentMessage.senderLongName);
        
        // Node ID on next line
        tft.setTextColor(ST77XX_YELLOW);
        tft.setCursor(20, 85);
        tft.print("Node: ");
        tft.setTextColor(ST77XX_WHITE);
        tft.printf("%08X", (unsigned int)currentMessage.nodeId);
    } else {
        // Node ID directly below if no long name
        tft.setTextColor(ST77XX_YELLOW);
        tft.setCursor(20, 70);
        tft.print("Node: ");
        tft.setTextColor(ST77XX_WHITE);
        tft.printf("%08X", (unsigned int)currentMessage.nodeId);
    }
    
    // Time received (adjust position based on whether long name is shown)
    int timeY = (currentMessage.senderLongName.length() > 0 && 
                 currentMessage.senderLongName != currentMessage.senderName) ? 100 : 85;
    unsigned long secondsAgo = (millis() - currentMessage.timestamp) / 1000;
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(20, timeY);
    tft.print("Time: ");
    tft.setTextColor(ST77XX_WHITE);
    if (secondsAgo < 60) {
        tft.printf("%lus ago", secondsAgo);
    } else if (secondsAgo < 3600) {
        tft.printf("%lum ago", secondsAgo / 60);
    } else {
        tft.printf("%luh ago", secondsAgo / 3600);
    }
    
    // Message separator (adjust position)
    int separatorY = timeY + 15;
    tft.drawLine(20, separatorY, 300, separatorY, ST77XX_YELLOW);
    
    // Message content with word wrap
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    
    // Simple word wrap for popup (smaller area)
    String wrappedMessage = currentMessage.messageText;
    int lineLength = 45; // Characters per line in popup
    // Start message below separator (separatorY already defined above)
    int yPos = separatorY + 15;
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
    tft.print("[ANY KEY] Dismiss Message");
}

void MessagePopupScreen::updateTimestamp(Adafruit_ST7789& tft, unsigned long currentTime) {
    if (!hasMessage) return;
    
    // Calculate new timestamp value
    unsigned long secondsAgo = (currentTime - currentMessage.timestamp) / 1000;
    
    // Only redraw if timestamp changed
    if (secondsAgo == lastDrawnTimestamp) {
        return;  // No change, skip redraw
    }
    
    lastDrawnTimestamp = secondsAgo;
    
    // Calculate time position based on long name presence
    int timeY = (currentMessage.senderLongName.length() > 0 && 
                 currentMessage.senderLongName != currentMessage.senderName) ? 100 : 85;
    
    // Clear the entire time line from "Time: " onwards (from x=20 to end)
    tft.fillRect(20, timeY, 280, 12, ST77XX_BLACK);
    
    // Redraw the complete time line
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(20, timeY);
    tft.print("Time: ");
    
    tft.setTextColor(ST77XX_WHITE);
    if (secondsAgo < 60) {
        tft.printf("%lus ago", secondsAgo);
    } else if (secondsAgo < 3600) {
        tft.printf("%lum ago", secondsAgo / 60);
    } else {
        tft.printf("%luh ago", secondsAgo / 3600);
    }
    
    lastTimestampUpdate = currentTime;
}

void MessagePopupScreen::updateCounter(Adafruit_ST7789& tft, int currentIndex, int totalMessages) {
    if (!hasMessage) return;
    
    // Update the message data
    currentMessage.currentIndex = currentIndex;
    currentMessage.totalMessages = totalMessages;
    
    // Force counter redraw by resetting tracking
    int oldCurrent = lastDrawnCurrentIndex;
    int oldTotal = lastDrawnTotalMessages;
    lastDrawnCurrentIndex = 0;
    lastDrawnTotalMessages = 0;
    
    // Clear old counter area
    tft.fillRect(240, 20, 70, 20, ST77XX_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(250, 25);
    tft.printf("(%d/%d)", currentIndex, totalMessages);
    
    // Update tracking
    lastDrawnCurrentIndex = currentIndex;
    lastDrawnTotalMessages = totalMessages;
    
    LOG_DEBUG("🔧 UI: Counter updated from (%d/%d) to (%d/%d)", oldCurrent, oldTotal, currentIndex, totalMessages);
}
