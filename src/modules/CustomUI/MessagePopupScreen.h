#pragma once

#include "BaseScreen.h"
#include <Arduino.h>

/**
 * Message structure for popup display
 */
struct MessageData {
    String messageText;
    String senderName;
    String senderLongName;
    uint32_t nodeId;
    unsigned long timestamp;
    int currentIndex;
    int totalMessages;
};

/**
 * Message Popup Screen - Displays incoming message notifications
 * Not part of the navigation stack, shown as overlay
 */
class MessagePopupScreen : public BaseScreen {
public:
    MessagePopupScreen(UINavigator* navigator);
    
    // Screen lifecycle
    virtual void onEnter() override;
    virtual void onExit() override;
    
    // Input handling
    virtual void handleInput(uint8_t input) override;
    
    // Display update
    virtual bool needsUpdate(UIDataState& dataState) override;
    virtual void draw(Adafruit_ST7789& tft, UIDataState& dataState) override;
    
    // Message display methods
    void showMessage(const MessageData& msg);
    void updateTimestamp(Adafruit_ST7789& tft, unsigned long currentTime);
    void updateCounter(Adafruit_ST7789& tft, int currentIndex, int totalMessages);
    bool hasActiveMessage() const { return hasMessage; }
    
private:
    MessageData currentMessage;
    bool hasMessage;
    unsigned long lastTimestampUpdate;
    unsigned long lastDrawnTimestamp;  // Track last drawn timestamp to avoid redraw
    int lastDrawnCurrentIndex;
    int lastDrawnTotalMessages;
    
    void drawMessageContent(Adafruit_ST7789& tft);
};
