#include "DataStore.h"

#if defined(VARIANT_heltec_v3_custom)

#include "gps/RTC.h"
#include <algorithm>

DataStore& DataStore::getInstance() {
    static DataStore instance;
    return instance;
}

DataStore::DataStore() : needsSort(false) {
    // Reserve space for messages to avoid frequent reallocations
    messages.reserve(MAX_MESSAGES);
    LOG_INFO("🔧 DATASTORE: Initialized with capacity for %d messages", MAX_MESSAGES);
}

void DataStore::addMessage(const MessageInfo& message) {
    // Validate message
    if (!message.isValid || strlen(message.text) == 0) {
        LOG_DEBUG("🔧 DATASTORE: Skipping invalid message");
        return;
    }
    
    // Check for duplicate messages (same text, sender, and timestamp within 5 seconds)
    for (const auto& existing : messages) {
        if (existing.senderNodeId == message.senderNodeId &&
            strcmp(existing.text, message.text) == 0 &&
            abs((int32_t)(existing.timestamp - message.timestamp)) < 5) {
            LOG_DEBUG("🔧 DATASTORE: Skipping duplicate message");
            return;
        }
    }
    
    // Add the message
    messages.push_back(message);
    needsSort = true;
    
    LOG_INFO("🔧 DATASTORE: Added message from %s: \"%.30s%s\" (total: %d)", 
             message.senderName, 
             message.text,
             strlen(message.text) > 30 ? "..." : "",
             messages.size());
    
    // Enforce size limit
    enforceMaxSize();
    
    // Log storage stats periodically
    if (messages.size() % 10 == 0) {
        logStorageStats();
    }
}

std::vector<MessageInfo> DataStore::getRecentMessages(int maxMessages) const {
    if (messages.empty()) {
        return std::vector<MessageInfo>();
    }
    
    // Sort messages if needed (most recent first)
    if (needsSort) {
        sortMessagesByTimestamp();
    }
    
    // Return the requested number of most recent messages
    size_t count = std::min((size_t)maxMessages, messages.size());
    std::vector<MessageInfo> result;
    result.reserve(count);
    
    // Take the most recent messages (already sorted most recent first)
    for (size_t i = 0; i < count; i++) {
        result.push_back(messages[i]);
    }
    
    LOG_DEBUG("🔧 DATASTORE: Returning %d recent messages (of %d total)", result.size(), messages.size());
    return result;
}

size_t DataStore::getMessageCount() const {
    return messages.size();
}

void DataStore::clearMessages() {
    messages.clear();
    needsSort = false;
    LOG_INFO("🔧 DATASTORE: Cleared all messages");
}

bool DataStore::hasMessages() const {
    return !messages.empty();
}

MessageInfo DataStore::getLatestMessage() const {
    if (messages.empty()) {
        return MessageInfo(); // Return invalid message
    }
    
    // Sort messages if needed
    if (needsSort) {
        sortMessagesByTimestamp();
    }
    
    // Return the most recent message (first in sorted list)
    return messages[0];
}

void DataStore::sortMessagesByTimestamp() const {
    if (!needsSort || messages.empty()) {
        return;
    }
    
    // Sort messages by timestamp (most recent first)
    std::sort(const_cast<std::vector<MessageInfo>&>(messages).begin(), 
              const_cast<std::vector<MessageInfo>&>(messages).end(),
              [](const MessageInfo& a, const MessageInfo& b) {
                  return a.timestamp > b.timestamp;
              });
    
    needsSort = false;
    LOG_DEBUG("🔧 DATASTORE: Sorted %d messages by timestamp", messages.size());
}

void DataStore::enforceMaxSize() {
    if (messages.size() <= MAX_MESSAGES) {
        return;
    }
    
    // Sort first to ensure we keep the most recent messages
    if (needsSort) {
        sortMessagesByTimestamp();
    }
    
    // Remove oldest messages (keep MAX_MESSAGES most recent)
    size_t oldSize = messages.size();
    messages.resize(MAX_MESSAGES);
    
    LOG_INFO("🔧 DATASTORE: Trimmed message history from %d to %d messages", oldSize, MAX_MESSAGES);
}

void DataStore::logStorageStats() const {
    size_t totalTextSize = 0;
    size_t directMessages = 0;
    size_t channelMessages = 0;
    
    for (const auto& msg : messages) {
        totalTextSize += strlen(msg.text);
        if (msg.isDirectMessage) {
            directMessages++;
        } else {
            channelMessages++;
        }
    }
    
    // Calculate memory usage
    size_t structSize = sizeof(MessageInfo) * messages.size();
    size_t vectorOverhead = messages.capacity() * sizeof(MessageInfo) - structSize;
    
    LOG_INFO("🔧 DATASTORE Stats: %d msgs (%d DM, %d CH), %d chars, ~%d bytes", 
             messages.size(), directMessages, channelMessages, totalTextSize, 
             structSize + vectorOverhead);
}

#endif