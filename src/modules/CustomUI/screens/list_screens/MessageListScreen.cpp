#include "MessageListScreen.h"
#include "gps/RTC.h" // for getTime() function
#include <Arduino.h>
#include <algorithm>

// Logging macro
#ifndef LOG_INFO
#define LOG_INFO(format, ...) Serial.printf("[INFO] " format "\n", ##__VA_ARGS__)
#endif

MessageListScreen::MessageListScreen() : BaseListScreen("Messages", 20) {
    // Set navigation hints
    std::vector<NavHint> hints;
    hints.push_back(NavHint('A', "Back"));
    hints.push_back(NavHint('1', "Details"));
    setNavigationHints(hints);
    
    isLoading = false;
    lastRefreshTime = 0;
    
    LOG_INFO("💬 MessageListScreen: Created");
}

MessageListScreen::~MessageListScreen() {
    LOG_INFO("💬 MessageListScreen: Destroyed");
}

void MessageListScreen::onEnter() {
    LOG_INFO("💬 MessageListScreen: Entering screen");
    
    // Call parent onEnter
    BaseListScreen::onEnter();
    
    // Initialize message state
    messages.clear();
    isLoading = false;
    
    // Load messages on next update cycle
    lastRefreshTime = 0; // This will trigger refresh in onBeforeDrawItems
    
    LOG_INFO("💬 MessageListScreen: Screen ready, messages will load on next update");
}

void MessageListScreen::onExit() {
    LOG_INFO("💬 MessageListScreen: Exiting screen - cleaning memory");
    
    // Call parent onExit
    BaseListScreen::onExit();
    
    // Force complete vector deallocation
    messages.clear();
    messages.shrink_to_fit();
    std::vector<MessageInfo>().swap(messages);
    
    // Reset message state
    isLoading = false;
    lastRefreshTime = 0;
    
    // Log memory cleanup
    LOG_INFO("💬 MessageListScreen: Vector memory deallocated, state reset");
}

void MessageListScreen::onBeforeDrawItems(lgfx::LGFX_Device& tft) {
    // Refresh message list periodically or on first load
    unsigned long now = millis();
    if (lastRefreshTime == 0 || (now - lastRefreshTime > 10000)) { // Refresh every 10 seconds
        refreshMessageList();
        lastRefreshTime = now;
    }
    
    if (isLoading) {
        // BaseListScreen will handle clearing - just show loading message
        tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
        tft.setTextSize(1);
        tft.setCursor(10, getContentY() + 20);
        tft.print("Loading messages...");
        return;
    }
    
    if (messages.empty()) {
        // BaseListScreen will handle clearing - just show no messages message
        tft.setTextColor(COLOR_DARK_RED, COLOR_BLACK);
        tft.setTextSize(1);
        tft.setCursor(10, getContentY() + 20);
        tft.print("No messages found");
        
        tft.setTextColor(COLOR_DIM_GREEN, COLOR_BLACK);
        tft.setCursor(10, getContentY() + 40);
        tft.print("Messages will appear here");
        return;
    }
}

bool MessageListScreen::handleKeyPress(char key) {
    LOG_INFO("💬 MessageListScreen: Key pressed: %c (isLoading: %s, messages: %d)", 
        key, isLoading ? "true" : "false", messages.size());
    
    if (isLoading) {
        return true; // Ignore keys while loading
    }
    
    switch (key) {
        case '1':
            LOG_INFO("💬 MessageListScreen: Details button pressed");
            // Don't handle '1' here, let CustomUIModule handle navigation
            return false;
            
        case 'A':
        case 'a':
            LOG_INFO("💬 MessageListScreen: Back button pressed");
            // Will be handled by CustomUIModule for navigation back
            return false;
            
        case '#':
            LOG_INFO("💬 MessageListScreen: Refreshing message list");
            refreshMessageList();
            return true;
            
        default:
            // Let BaseListScreen handle navigation (arrow keys, select)
            return BaseListScreen::handleKeyPress(key);
    }
}

void MessageListScreen::refreshMessageList() {
    LOG_INFO("💬 MessageListScreen: Refreshing message list");
    isLoading = true;
    
    // Get messages from LoRa helper
    std::vector<MessageInfo> newMessages = LoRaHelper::getRecentMessages(15);
    
    // Only update if data actually changed
    bool dataChanged = (newMessages.size() != messages.size());
    if (!dataChanged) {
        // Check if any message data changed
        for (size_t i = 0; i < newMessages.size() && i < messages.size(); i++) {
            if (newMessages[i].timestamp != messages[i].timestamp || 
                strcmp(newMessages[i].text, messages[i].text) != 0) {
                dataChanged = true;
                break;
            }
        }
    }
    
    if (dataChanged) {
        messages = newMessages;
        
        // Reset selection if current selection is out of bounds
        if (getSelectedIndex() >= static_cast<int>(messages.size())) {
            setSelection(std::max(0, static_cast<int>(messages.size()) - 1));
        }
        
        // Only invalidate list if data actually changed
        invalidateList();
        LOG_INFO("💬 MessageListScreen: Data changed, list invalidated");
    }
    
    isLoading = false;
    LOG_INFO("💬 MessageListScreen: Refresh completed, found %d messages (changed: %s)", 
             messages.size(), dataChanged ? "yes" : "no");
}

void MessageListScreen::onItemSelected(int index) {
    // Selection is now handled by CustomUIModule navigation logic
    LOG_INFO("💬 MessageListScreen: Item %d selected", index);
}

int MessageListScreen::getItemCount() {
    return static_cast<int>(messages.size());
}

void MessageListScreen::drawItem(lgfx::LGFX_Device& tft, int index, int y, bool isSelected) {
    if (index < 0 || index >= static_cast<int>(messages.size())) {
        return; // Invalid index
    }
    
    const MessageInfo& msg = messages[index];
    
    // BaseListScreen needs COLOR_SELECTION constant
    static const uint16_t COLOR_SELECTION = 0x4208; // Dim green for selection
    
    // Background color
    uint16_t bgColor = isSelected ? COLOR_SELECTION : COLOR_BLACK;
    
    // Message type indicator (first 15px) - Green for DM, Red for Channel
    uint16_t typeColor = msg.isDirectMessage ? COLOR_GREEN : COLOR_RED;
    if (isSelected) {
        typeColor = msg.isDirectMessage ? 0xFFFF : 0xFFFF; // White when selected
    }
    
    tft.fillRect(5, y + 5, 8, 8, typeColor);
    
    // Sender name (main area)
    uint16_t textColor = isSelected ? 0xFFFF : (msg.isDirectMessage ? COLOR_GREEN : COLOR_RED);
    tft.setTextColor(textColor, bgColor);
    tft.setTextSize(1);
    
    // Create display name with channel info
    String displayName;
    if (msg.isDirectMessage) {
        displayName = "DM: " + String(msg.senderName);
    } else {
        displayName = "#" + String(msg.channelName) + " " + String(msg.senderName);
    }
    
    // Truncate if too long
    if (displayName.length() > 18) {
        displayName = displayName.substring(0, 15) + "...";
    }
    
    tft.setCursor(20, y + 3);
    tft.print(displayName);
    
    // Time ago (second line)
    String timeStr = formatTimeSince(msg.timestamp);
    uint16_t timeColor = isSelected ? 0xC618 : COLOR_DIM_GREEN; // Light grey when selected
    
    tft.setTextColor(timeColor, bgColor);
    tft.setCursor(20, y + 12);
    tft.setTextSize(1);
    tft.print(timeStr);
    
    // Message preview (right side)
    String messageText = String(msg.text);
    if (messageText.length() > 15) {
        messageText = messageText.substring(0, 12) + "...";
    }
    
    uint16_t msgColor = isSelected ? 0xFFFF : 0xCCCC; // White when selected, light gray otherwise
    tft.setTextColor(msgColor, bgColor);
    tft.setCursor(160, y + 7);
    tft.print(messageText);
}

MessageInfo MessageListScreen::getSelectedMessage() const {
    int currentSelection = getSelectedIndex();
    LOG_INFO("💬 MessageListScreen: getSelectedMessage - selection: %d, total messages: %d", 
             currentSelection, static_cast<int>(messages.size()));
    
    if (currentSelection >= 0 && currentSelection < static_cast<int>(messages.size()) && !messages.empty()) {
        LOG_INFO("💬 MessageListScreen: Returning valid message from: %s", messages[currentSelection].senderName);
        return messages[currentSelection];
    }
    LOG_INFO("💬 MessageListScreen: Returning invalid MessageInfo");
    return MessageInfo(); // Return invalid MessageInfo
}

bool MessageListScreen::hasValidSelection() const {
    int currentSelection = getSelectedIndex();
    return currentSelection >= 0 && 
           currentSelection < static_cast<int>(messages.size()) && 
           !messages.empty();
}

String MessageListScreen::formatTimeSince(uint32_t timestamp) {
    if (timestamp == 0) {
        return "Unknown";
    }
    
    uint32_t now = getTime();
    if (now == 0) now = millis() / 1000; // Fallback if RTC not available
    
    uint32_t elapsed = now - timestamp;
    
    if (elapsed < 60) {
        return String(elapsed) + "s ago";
    } else if (elapsed < 3600) { // Less than 1 hour
        int minutes = elapsed / 60;
        return String(minutes) + "m ago";
    } else if (elapsed < 86400) { // Less than 1 day
        int hours = elapsed / 3600;
        return String(hours) + "h ago";
    } else { // Days
        int days = elapsed / 86400;
        return String(days) + "d ago";
    }
}