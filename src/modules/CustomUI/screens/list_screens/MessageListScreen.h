#pragma once

#include "BaseListScreen.h"
#include "../utils/LoRaHelper.h"
#include <vector>

/**
 * Message List Screen - Shows recent mesh messages
 * Features:
 * - Scrollable list of recent messages  
 * - Color coding: Green for DMs, Red for channel messages
 * - Time since received display
 * - Horizontal scrolling for long messages when selected
 * - Navigation: [A] Back, [Left/Right] Scroll message
 */
class MessageListScreen : public BaseListScreen {
public:
    MessageListScreen();
    virtual ~MessageListScreen();

    // BaseListScreen interface  
    virtual void onEnter() override;
    virtual void onExit() override;
    virtual bool handleKeyPress(char key) override;

protected:
    // BaseListScreen abstract methods
    virtual void drawItem(lgfx::LGFX_Device& tft, int index, int y, bool isSelected) override;
    virtual int getItemCount() override;
    virtual void onItemSelected(int index) override;
    virtual void onBeforeDrawItems(lgfx::LGFX_Device& tft) override;

private:
    /**
     * Refresh the message list from mesh database
     */
    void refreshMessageList();
    
    /**
     * Format time since message for display
     */
    String formatTimeSince(uint32_t timestamp);

    // Message data
    std::vector<MessageInfo> messages;
    
    // UI state
    bool isLoading;         // Currently loading flag
    unsigned long lastRefreshTime;
    
    // Colors
    static const uint16_t COLOR_BLACK = 0x0000;
    static const uint16_t COLOR_GREEN = 0x07E0;
    static const uint16_t COLOR_RED = 0xF800;
    static const uint16_t COLOR_YELLOW = 0xFFE0;
    static const uint16_t COLOR_DIM_GREEN = 0x4208;
    static const uint16_t COLOR_DARK_RED = 0x7800;
    static const uint16_t COLOR_GRAY = 0x8410;
};