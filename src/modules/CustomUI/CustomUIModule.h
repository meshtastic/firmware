#pragma once

#include "configuration.h"

#if defined(VARIANT_heltec_v3_custom)

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include <Adafruit_ST7789.h>
#include <Arduino.h>
#include <Keypad.h>

// Forward declarations
class UINavigator;
class MessagePopupScreen;

/**
 * Message structure for queued incoming messages
 */
struct QueuedMessage {
    String messageText;
    String senderName;
    String senderLongName;
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
    MessagePopupScreen* messagePopupScreen;
    
    // Button handling
    int lastButtonState;
    unsigned long lastButtonCheck;
    void checkButtonInput();
    
    // Keypad handling
    static const byte ROWS = 4;
    static const byte COLS = 4;
    static char keys[ROWS][COLS];
    static byte rowPins[ROWS];
    static byte colPins[COLS];
    Keypad* keypad;
    void checkKeypadInput();
    
    // Message queue system (5 message limit)
    static const int MAX_QUEUED_MESSAGES = 5;
    QueuedMessage messageQueue[MAX_QUEUED_MESSAGES];
    int queueHead; // Next position to write
    int queueTail; // Next position to read
    int queueSize; // Current number of messages
    
    // Message popup display
    bool showingMessagePopup;
    int currentMessageIndex;
    unsigned long lastTimestampUpdate;
    unsigned long initializationTime;  // Track when initialization completed
    
    // Message management methods
    bool addMessageToQueue(const String& messageText, const String& senderName, const String& senderLongName, uint32_t nodeId);
    bool hasUnreadMessages() const;
    void showNextMessage();
    void dismissCurrentMessage();
    void drawMessageCounter();
};

// Global setup function
void setup_CustomUIModule();

// Global instance
extern CustomUIModule *customUIModule;

#endif