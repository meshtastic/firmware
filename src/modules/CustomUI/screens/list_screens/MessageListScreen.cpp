#include "MessageListScreen.h"
#include "../BaseScreen.h"
#include "../utils/LoRaHelper.h"
#include "configuration.h"
#include <RTC.h>
#include <algorithm>

// Logging macro
#ifndef LOG_INFO
#define LOG_INFO(format, ...) Serial.printf("[INFO] " format "\n", ##__VA_ARGS__)
#endif
#ifndef LOG_DEBUG
#define LOG_DEBUG(format, ...) Serial.printf("[DEBUG] " format "\n", ##__VA_ARGS__)
#endif

MessageListScreen::MessageListScreen() 
    : BaseListScreen("Messages", 24), lastRefreshTime(0) {
    LOG_INFO("🔧 MessageListScreen: Initialized");
}

MessageListScreen::~MessageListScreen() {
    messages.clear();
}

void MessageListScreen::onEnter() {
    LOG_INFO("🔧 MessageListScreen: Entering screen");
    BaseListScreen::onEnter();
    
    // Always refresh when entering screen
    refreshMessageList();
}

void MessageListScreen::onExit() {
    LOG_INFO("🔧 MessageListScreen: Exiting screen");
    BaseListScreen::onExit();
}

void MessageListScreen::onBeforeDrawItems(lgfx::LGFX_Device& tft) {
    // Check if we need to refresh the message list
    unsigned long currentTime = millis();
    if (currentTime - lastRefreshTime > REFRESH_INTERVAL) {
        refreshMessageList();
    }
    
    // Show loading text if no messages
    if (messages.empty()) {
        tft.setTextColor(0x8410); // Gray color
        tft.setFont(&fonts::Font2);
        tft.drawString("No messages found", 10, listStartY + 20);
        tft.drawString("Messages will appear here", 10, listStartY + 45);
        tft.drawString("when received", 10, listStartY + 70);
    }
}

void MessageListScreen::drawItem(lgfx::LGFX_Device& tft, int index, int y, bool isSelected) {
    if (index >= messages.size()) {
        return;
    }
    
    const MessageDisplayItem& msg = messages[index];
    
    // Clear the item area first
    tft.fillRect(0, y, getContentWidth(), itemHeight, COLOR_BLACK);
    
    // Set colors based on selection
    uint16_t textColor = isSelected ? 0xFFE0 : 0xFFFF;  // Yellow when selected, white otherwise
    uint16_t senderColor = isSelected ? 0x07E0 : 0x8410; // Green when selected, gray otherwise
    uint16_t timeColor = isSelected ? 0x07E0 : 0x6B6D;   // Green when selected, dim gray otherwise
    
    // Draw sender name (top line)
    tft.setTextColor(senderColor);
    tft.setFont(&fonts::Font2);
    tft.drawString(msg.senderName, 5, y + 2);
    
    // Draw time ago (top right)
    tft.setTextColor(timeColor);
    String timeStr = msg.timeAgo;
    int timeWidth = tft.textWidth(timeStr);
    tft.drawString(timeStr, getContentWidth() - timeWidth - 5, y + 2);
    
    // Draw message preview (bottom line)
    tft.setTextColor(textColor);
    tft.setFont(&fonts::Font0);
    tft.drawString(msg.text, 5, y + 14);
    
    LOG_DEBUG("🔧 MessageListScreen: Drew item %d: sender='%s' time='%s'", 
              index, msg.senderName.c_str(), msg.timeAgo.c_str());
}

int MessageListScreen::getItemCount() {
    return messages.size();
}

void MessageListScreen::onItemSelected(int index) {
    if (index >= 0 && index < messages.size()) {
        LOG_INFO("🔧 MessageListScreen: Selected message %d from %s", 
                 index, messages[index].senderName.c_str());
        // Future: Open message detail view
    }
}

void MessageListScreen::refreshMessageList() {
    LOG_INFO("🔧 MessageListScreen: Refreshing message list");
    
    messages.clear();
    lastRefreshTime = millis();
    
    // Get recent messages from LoRaHelper
    auto recentMessages = LoRaHelper::getRecentMessages(MAX_MESSAGES);
    
    // Convert MessageInfo to MessageDisplayItem
    for (const auto& msgInfo : recentMessages) {
        if (!msgInfo.isValid) continue;
        
        MessageDisplayItem displayItem(
            String(msgInfo.text),
            String(msgInfo.senderName),
            LoRaHelper::formatTimeAgo(msgInfo.timestamp),
            msgInfo.timestamp,
            msgInfo.senderNodeId,
            msgInfo.isOutgoing
        );
        messages.push_back(displayItem);
    }
    
    // Sort by timestamp (newest first)
    sortMessagesByTime();
    
    // Reset selection to top
    setSelection(0);
    invalidateList();
    
    LOG_INFO("🔧 MessageListScreen: Loaded %d messages", messages.size());
}

String MessageListScreen::formatSenderName(uint32_t nodeId, bool isOutgoing) {
    // Delegate to LoRaHelper
    return LoRaHelper::formatSenderName(nodeId, isOutgoing);
}

String MessageListScreen::formatTimeAgo(uint32_t timestamp) {
    // Delegate to LoRaHelper
    return LoRaHelper::formatTimeAgo(timestamp);
}

void MessageListScreen::sortMessagesByTime() {
    // Sort by timestamp, newest first
    std::sort(messages.begin(), messages.end(), 
              [](const MessageDisplayItem& a, const MessageDisplayItem& b) {
                  return a.timestamp > b.timestamp;
              });
}

String MessageListScreen::truncateText(const String& text, int maxLength) {
    if (text.length() <= maxLength) {
        return text;
    }
    
    return text.substring(0, maxLength - 3) + "...";
}