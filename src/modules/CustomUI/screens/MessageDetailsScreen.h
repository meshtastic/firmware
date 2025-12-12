#pragma once

#include "BaseScreen.h"
#include "utils/LoRaHelper.h"
#include <vector>

/**
 * Message Details Screen - Shows scrollable full message content
 * Features:
 * - Displays complete message text with scrollable content
 * - Similar styling to MessagesScreen (green sender, white text, yellow timestamp)
 * - Line-based content division with dirty rect optimization
 * - Navigation: [A] Back to MessageListScreen, [2] Scroll Up, [8] Scroll Down
 */
class MessageDetailsScreen : public BaseScreen {
public:
    MessageDetailsScreen();
    virtual ~MessageDetailsScreen();

    // BaseScreen interface
    virtual void onEnter() override;
    virtual void onExit() override;
    virtual void onDraw(lgfx::LGFX_Device& tft) override;
    virtual bool handleKeyPress(char key) override;

    /**
     * Set the message to display
     * @param msgInfo MessageInfo object containing message data
     */
    void setMessage(const MessageInfo& msgInfo);

    /**
     * Check if message is set and valid
     * @return true if message is loaded and valid
     */
    bool hasValidMessage() const;
    
    /**
     * Get the current message info for reply purposes
     * @return current MessageInfo object
     */
    const MessageInfo& getCurrentMessage() const;

private:
    // Scrolling and layout constants
    static const int LINE_HEIGHT = 20;          // Height per line of text
    static const int SCROLL_MARGIN = 10;        // Margin for scrolling
    static const int SENDER_HEIGHT = 30;        // Height for sender name section
    static const int TIMESTAMP_HEIGHT = 25;     // Height for timestamp section
    static const int TEXT_AREA_HEIGHT = CONTENT_HEIGHT - SENDER_HEIGHT - TIMESTAMP_HEIGHT - 20; // Space for message text

    // Message data
    MessageInfo currentMessage;
    bool messageSet;

    // Scrolling state
    std::vector<String> textLines;      // Wrapped text lines
    int scrollOffset;                   // Current scroll position (line index)
    int maxVisibleLines;                // Maximum lines that can be displayed
    int totalLines;                     // Total number of text lines

    // Dirty rect tracking
    bool contentDirty;                  // Content area needs redraw
    bool headerDirty;                   // Header (sender) area needs redraw
    bool footerDirty;                   // Footer (timestamp) area needs redraw

    /**
     * Wrap message text into lines based on display width
     */
    void wrapTextToLines();

    /**
     * Calculate maximum visible lines in text area
     */
    void calculateVisibleLines();

    /**
     * Draw sender information section
     */
    void drawSenderSection(lgfx::LGFX_Device& tft);

    /**
     * Draw scrollable message text section with dirty rect optimization
     */
    void drawTextSection(lgfx::LGFX_Device& tft);

    /**
     * Draw timestamp section
     */
    void drawTimestampSection(lgfx::LGFX_Device& tft);

    /**
     * Handle scrolling up/down
     */
    void scrollUp();
    void scrollDown();

    /**
     * Update navigation hints based on scroll position
     */
    void updateNavigationHints();

    /**
     * Clear all content and reset state
     */
    void clearContent();

    // Colors (matching MessagesScreen)
    static const uint16_t COLOR_BLACK = 0x0000;
    static const uint16_t COLOR_WHITE = 0xFFFF;
    static const uint16_t COLOR_GREEN = 0x07E0;     // Sender name
    static const uint16_t COLOR_YELLOW = 0xFFE0;    // Timestamp
    static const uint16_t COLOR_GRAY = 0x8410;      // Disabled/secondary text
};