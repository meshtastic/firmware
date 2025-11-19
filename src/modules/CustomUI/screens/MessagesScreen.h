#pragma once
#include "BaseScreen.h"
#include <vector>
#include <LovyanGFX.hpp>
#include <Arduino.h>

struct MessageEntry {
    String text;
    String sender;
    unsigned long timestamp;
    MessageEntry(const String& t, const String& s, unsigned long ts)
        : text(t), sender(s), timestamp(ts) {}
};

class MessagesScreen : public BaseScreen {
public:
    MessagesScreen();
    virtual ~MessagesScreen();

    void onEnter() override;
    void onExit() override;
    void onDraw(lgfx::LGFX_Device& tft) override;
    bool handleKeyPress(char key) override;

    // Call to update the relative time display (should be called every minute)
    void updateRelativeTime();

    // Add a new message to the buffer
    void addMessage(const String& text, const String& sender, unsigned long timestamp);
    bool hasMessages() const;
    void clearMessages();

private:
    static const int MAX_MESSAGES = 10;
    std::vector<MessageEntry> buffer;
    int currentIndex; // 0 = newest, buffer.size()-1 = oldest

    void showPrev();
    void updateNavHint();
};
