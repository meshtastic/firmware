#pragma once

#include "BaseListScreen.h"
#include "../utils/LoRaHelper.h"
#include <vector>
#include <String>

/**
 * MessageListScreen - Displays stored messages from Meshtastic system
 * 
 * Features:
 * - Shows messages from devicestate.rx_text_message and toPhoneQueue
 * - Displays sender info (long name or fallback to short name)
 * - Shows "X seconds ago" timestamp formatting
 * - Supports navigation through message list
 * - Future: Will support message viewing and deletion
 * 
 * Message Sources:
 * - Recent text message from devicestate
 * - Pending messages in phone queue
 * - StoreForward history (if module enabled)
 */
class MessageListScreen : public BaseListScreen {
public:
    MessageListScreen();
    virtual ~MessageListScreen();
    
    // BaseScreen interface
    virtual void onEnter() override;
    virtual void onExit() override;
    
    // BaseListScreen interface
    virtual void drawItem(lgfx::LGFX_Device& tft, int index, int y, bool isSelected) override;
    virtual int getItemCount() override;
    virtual void onItemSelected(int index) override;
    virtual void onBeforeDrawItems(lgfx::LGFX_Device& tft) override;

private:
    /**
     * Internal message structure for display
     */
    struct MessageDisplayItem {
        String text;                    // Message content (truncated if needed)
        String senderName;              // Formatted sender name 
        String timeAgo;                 // "X seconds ago" format
        uint32_t timestamp;             // Original timestamp
        uint32_t senderNodeId;          // Sender node ID
        bool isOutgoing;                // True if sent by us
        
        MessageDisplayItem() : timestamp(0), senderNodeId(0), isOutgoing(false) {}
        MessageDisplayItem(const String& txt, const String& sender, const String& time, 
                          uint32_t ts, uint32_t nodeId, bool outgoing)
            : text(txt), senderName(sender), timeAgo(time), timestamp(ts), 
              senderNodeId(nodeId), isOutgoing(outgoing) {}
    };
    
    std::vector<MessageDisplayItem> messages;
    unsigned long lastRefreshTime;
    
    // Helper methods
    void refreshMessageList();
    void sortMessagesByTime();
    String formatSenderName(uint32_t nodeId, bool isOutgoing);
    String formatTimeAgo(uint32_t timestamp);
    String truncateText(const String& text, int maxLength);
    
    // Constants
    static const int MAX_MESSAGES = 50;           // Maximum messages to display
    static const int TEXT_PREVIEW_LENGTH = 45;   // Max characters for message preview
    static const unsigned long REFRESH_INTERVAL = 60000; // Refresh every 60 seconds
};