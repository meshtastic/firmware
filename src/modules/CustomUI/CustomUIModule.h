#pragma once

#include "configuration.h"

#if defined(VARIANT_heltec_v3_custom)

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include <Adafruit_ST7789.h>
#include <Arduino.h>

// Forward declaration
class UINavigator;

/**
 * Message structure for queued incoming messages
 */
struct QueuedMessage {
    String messageText;
    String senderName;
    uint32_t nodeId;
    unsigned long timestamp;
    bool isRead;
};

/**
 * Custom UI Module for external ST7789 display
 * Handles display initialization, button input, and message display with popup queue
 */
class CustomUIModule : public SinglePortModule, private concurrency::OSThread {
public:
    CustomUIModule();
    
    // Module interface
    virtual int32_t runOnce() override;
    virtual bool wantUIFrame() override;
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    
    // Display management
    void initDisplay();
    
private:
    Adafruit_ST7789 tft;
    UINavigator* navigator;
    
    // Button handling
    int lastButtonState;
    unsigned long lastButtonCheck;
    void checkButtonInput();
    
    // Message queue system (10 message limit)
    static const int MAX_QUEUED_MESSAGES = 10;
    QueuedMessage messageQueue[MAX_QUEUED_MESSAGES];
    int queueHead; // Next position to write
    int queueTail; // Next position to read
    int queueSize; // Current number of messages
    
    // Message popup display
    bool showingMessagePopup;
    int currentMessageIndex;
    bool popupNeedsRedraw;
    unsigned long lastTimestampUpdate;
    
    // Message management methods
    bool addMessageToQueue(const String& messageText, const String& senderName, uint32_t nodeId);
    bool hasUnreadMessages() const;
    void showNextMessage();
    void dismissCurrentMessage();
    void drawMessagePopup();
    void updateTimestamp(); // Dirty rectangle update for timestamp only
    void drawMessageCounter();
};

// Global setup function
void setup_CustomUIModule();

// Global instance
extern CustomUIModule *customUIModule;

#endif