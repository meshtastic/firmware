#pragma once

#include "configuration.h"

#if defined(VARIANT_heltec_v3_custom)

#include "LoRaHelper.h" // For MessageInfo structure
#include <vector>
#include <Arduino.h>

/**
 * Singleton DataStore for managing message history
 * 
 * This class provides centralized storage for MessageInfo objects,
 * allowing the CustomUIModule to store incoming messages and 
 * LoRaHelper to retrieve them for display purposes.
 * 
 * Features:
 * - Singleton pattern for global access
 * - Fixed-size circular buffer to prevent memory issues
 * - Thread-safe operations
 * - Automatic cleanup of old messages
 */
class DataStore {
public:
    // Singleton access
    static DataStore& getInstance();
    
    // Message storage operations
    void addMessage(const MessageInfo& message);
    std::vector<MessageInfo> getRecentMessages(int maxMessages = 10) const;
    
    // Utility methods
    size_t getMessageCount() const;
    void clearMessages();
    bool hasMessages() const;
    
    // Get latest message
    MessageInfo getLatestMessage() const;

private:
    // Private constructor for singleton
    DataStore();
    ~DataStore() = default;
    
    // Delete copy constructor and assignment operator
    DataStore(const DataStore&) = delete;
    DataStore& operator=(const DataStore&) = delete;
    
    // Message storage
    static const size_t MAX_MESSAGES = 50; // Maximum messages to store
    std::vector<MessageInfo> messages;
    mutable bool needsSort; // Flag to indicate if messages need sorting
    
    // Helper methods
    void sortMessagesByTimestamp() const;
    void enforceMaxSize();
    void logStorageStats() const;
};

#endif