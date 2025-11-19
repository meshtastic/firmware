#include "MessagesScreen.h"
#include "BaseScreen.h"

MessagesScreen::MessagesScreen() : BaseScreen("Messages"), currentIndex(0) {
    updateNavHint();
}

MessagesScreen::~MessagesScreen() {}

void MessagesScreen::onEnter() {
    currentIndex = 0;
    updateNavHint();
    forceRedraw();
}

void MessagesScreen::onExit() {}

void MessagesScreen::onDraw(lgfx::LGFX_Device& tft) {
    tft.fillRect(0, getContentY(), getContentWidth(), getContentHeight(), 0x0000);
    if (buffer.empty()) {
        tft.setTextColor(0xFFE0, 0x0000);
        tft.setCursor(20, getContentY() + 40);
        tft.print("No messages");
        return;
    }
    const MessageEntry& msg = buffer[currentIndex];
    int y = getContentY() + 8;
    // Sender name (bold/green)
    tft.setTextColor(0x07E0, 0x0000); // Green, black bg
    tft.setTextSize(2);
    tft.setCursor(10, y);
    tft.print(msg.sender);
    tft.setTextSize(1);

    // Message text (normal font, white)
    y += 28;
    tft.setTextColor(0xFFFF, 0x0000); // White, black bg
    tft.setCursor(10, y);
    tft.setTextSize(2);
    tft.print(msg.text);
    tft.setTextSize(1);

    // Timestamp (bottom left, yellow)
    unsigned long t = msg.timestamp / 1000;
    unsigned int h = (t / 3600) % 24;
    unsigned int m = (t / 60) % 60;
    unsigned int s = t % 60;
    char timebuf[16];
    snprintf(timebuf, sizeof(timebuf), "%02u:%02u:%02u", h, m, s);
    tft.setTextColor(0xFFE0, 0x0000); // Yellow, black bg
    tft.setCursor(10, getContentY() + getContentHeight() - 22);
    tft.print(timebuf);

    // Counter (bottom right, yellow)
    String counter = String(currentIndex + 1) + "/" + String(buffer.size());
    tft.setTextColor(0xFFE0, 0x0000);
    tft.setCursor(getContentWidth() - 60, getContentY() + getContentHeight() - 22);
    tft.print(counter);
}


bool MessagesScreen::handleKeyPress(char key) {
    if (key == 'A') {
        if (buffer.empty() || buffer.size() == 1 || currentIndex == buffer.size() - 1) {
            // Go home
            return false; // Let UI module handle screen switch
        } else {
            showPrev();
            return true;
        }
    }
    return false;
}

void MessagesScreen::addMessage(const String& text, const String& sender, unsigned long timestamp) {
    if (buffer.size() >= MAX_MESSAGES) {
        buffer.pop_back(); // Remove oldest
    }
    buffer.insert(buffer.begin(), MessageEntry(text, sender, timestamp));
    currentIndex = 0;
    updateNavHint();
    forceRedraw();
}

bool MessagesScreen::hasMessages() const {
    return !buffer.empty();
}

void MessagesScreen::clearMessages() {
    buffer.clear();
    currentIndex = 0;
    updateNavHint();
    forceRedraw();
}

void MessagesScreen::showPrev() {
    if (currentIndex < buffer.size() - 1) {
        currentIndex++;
        updateNavHint();
        forceRedraw();
    }
}

void MessagesScreen::updateNavHint() {
    navHints.clear();
    if (buffer.empty() || buffer.size() == 1 || currentIndex == buffer.size() - 1) {
        navHints.push_back(NavHint('A', "Home"));
    } else {
        navHints.push_back(NavHint('A', "Prev"));
    }
}
