#include "MessageDetailsScreen.h"
#include "BaseScreen.h"
#include "configuration.h"

// Logging macro
#ifndef LOG_INFO
#define LOG_INFO(format, ...) Serial.printf("[INFO] " format "\n", ##__VA_ARGS__)
#endif

MessageDetailsScreen::MessageDetailsScreen() 
    : BaseScreen("Message Details"), messageSet(false), scrollOffset(0), 
      maxVisibleLines(0), totalLines(0), contentDirty(true), headerDirty(true), footerDirty(true) {
    calculateVisibleLines();
    updateNavigationHints();
}

MessageDetailsScreen::~MessageDetailsScreen() {
    clearContent();
}

void MessageDetailsScreen::onEnter() {
    LOG_INFO("📱 MessageDetailsScreen: Entering screen");
    
    // Reset scroll position
    scrollOffset = 0;
    
    // Mark everything for redraw
    contentDirty = true;
    headerDirty = true;
    footerDirty = true;
    
    updateNavigationHints();
    forceRedraw();
}

void MessageDetailsScreen::onExit() {
    LOG_INFO("📱 MessageDetailsScreen: Exiting screen - cleaning memory");
    
    // Clear text lines vector to free memory
    textLines.clear();
    textLines.shrink_to_fit();
    std::vector<String>().swap(textLines);
    
    // Reset state
    scrollOffset = 0;
    totalLines = 0;
    contentDirty = true;
    headerDirty = true;
    footerDirty = true;
    
    LOG_INFO("📱 MessageDetailsScreen: Memory cleaned and state reset");
}

void MessageDetailsScreen::onDraw(lgfx::LGFX_Device& tft) {
    if (!hasValidMessage()) {
        // Clear content area and show "No message" text
        tft.fillRect(0, getContentY(), getContentWidth(), getContentHeight(), COLOR_BLACK);
        tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
        tft.setTextSize(2);
        tft.setCursor(20, getContentY() + 60);
        tft.print("No message to display");
        tft.setTextSize(1);
        return;
    }

    // Always redraw sender section to prevent disappearing
    if (headerDirty || contentDirty) {
        drawSenderSection(tft);
        headerDirty = false;
    }

    // Redraw content when needed
    if (contentDirty) {
        drawTextSection(tft);
        contentDirty = false;
    }

    // Redraw footer when needed
    if (footerDirty) {
        drawTimestampSection(tft);
        footerDirty = false;
    }
}

bool MessageDetailsScreen::handleKeyPress(char key) {
    if (!hasValidMessage()) {
        return false; // Let UI module handle navigation
    }

    switch (key) {
        case 'A':
            // Back button - always goes back to MessageListScreen
            return false; // Let UI module handle screen switch
            
        case '1':
            // Reply button - let UI module handle navigation to T9 input
            return false; // Let UI module handle screen switch
            
        case '2':
            // Scroll up (up arrow)
            scrollUp();
            return true;
            
        case '8':
            // Scroll down (down arrow)
            scrollDown();
            return true;
            
        default:
            return false;
    }
}

void MessageDetailsScreen::setMessage(const MessageInfo& msgInfo) {
    LOG_INFO("📱 MessageDetailsScreen: Setting message from sender: %s", msgInfo.senderName);
    
    currentMessage = msgInfo;
    messageSet = msgInfo.isValid;
    
    if (messageSet) {
        // Wrap text to lines and reset scroll
        wrapTextToLines();
        scrollOffset = 0;
        
        // Mark all sections for redraw
        contentDirty = true;
        headerDirty = true;
        footerDirty = true;
        
        updateNavigationHints();
        forceRedraw();
        
        LOG_INFO("📱 MessageDetailsScreen: Message set successfully, %d total lines", totalLines);
    } else {
        LOG_INFO("📱 MessageDetailsScreen: Invalid message provided");
        clearContent();
    }
}

bool MessageDetailsScreen::hasValidMessage() const {
    return messageSet && currentMessage.isValid;
}

const MessageInfo& MessageDetailsScreen::getCurrentMessage() const {
    return currentMessage;
}

void MessageDetailsScreen::wrapTextToLines() {
    textLines.clear();
    
    if (!hasValidMessage()) {
        totalLines = 0;
        return;
    }

    // Use pixel-based wrapping for accurate text fitting
    String messageText(currentMessage.text);
    const int TEXT_MARGIN = 10;              // Left margin
    const int SCROLLBAR_WIDTH = 20;          // Reserve space for scrollbar
    const int AVAILABLE_WIDTH = getContentWidth() - TEXT_MARGIN - SCROLLBAR_WIDTH; // 290px effective width
    const int CHAR_WIDTH = 12;               // Approximate width per character at size 2 font
    const int CHARS_PER_LINE = AVAILABLE_WIDTH / CHAR_WIDTH; // ~24 chars for safe wrapping
    
    if (messageText.length() <= CHARS_PER_LINE) {
        textLines.push_back(messageText);
    } else {
        int start = 0;
        int length = messageText.length();
        
        while (start < length) {
            // Skip leading spaces
            while (start < length && messageText.charAt(start) == ' ') {
                start++;
            }
            
            if (start >= length) break;
            
            int end = start + CHARS_PER_LINE;
            
            if (end >= length) {
                // Last line - take remaining text
                String lastLine = messageText.substring(start);
                if (lastLine.length() > 0) {
                    textLines.push_back(lastLine);
                }
                break;
            }
            
            // Find last space within limit for word boundary
            int lastSpace = -1;
            for (int i = std::min(end, length - 1); i >= start; i--) {
                if (messageText.charAt(i) == ' ') {
                    lastSpace = i;
                    break;
                }
            }
            
            if (lastSpace > start && (lastSpace - start) >= (CHARS_PER_LINE / 2)) {
                // Good break point found - use it
                textLines.push_back(messageText.substring(start, lastSpace));
                start = lastSpace + 1;
            } else {
                // No good space or space too close to start - break at character limit
                textLines.push_back(messageText.substring(start, end));
                start = end;
            }
        }
    }
    
    totalLines = textLines.size();
    LOG_INFO("📱 MessageDetailsScreen: Text wrapped into %d lines (width: %dpx, chars: %d)", 
             totalLines, AVAILABLE_WIDTH, CHARS_PER_LINE);
}

void MessageDetailsScreen::calculateVisibleLines() {
    maxVisibleLines = 5; // Fixed 5 lines per page for page-based scrolling
    LOG_INFO("📱 MessageDetailsScreen: Max visible lines per page: %d", maxVisibleLines);
}

void MessageDetailsScreen::drawSenderSection(lgfx::LGFX_Device& tft) {
    if (!hasValidMessage()) return;
    
    // Clear sender area
    int senderY = getContentY();
    tft.fillRect(0, senderY, getContentWidth(), SENDER_HEIGHT, COLOR_BLACK);
    
    // Draw sender name in green, bold style (size 2)
    tft.setTextColor(COLOR_GREEN, COLOR_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, senderY + 5);
    tft.print("From: ");
    tft.print(currentMessage.senderName);
    tft.setTextSize(1);
    
    LOG_INFO("📱 MessageDetailsScreen: Drew sender section");
}

void MessageDetailsScreen::drawTextSection(lgfx::LGFX_Device& tft) {
    if (!hasValidMessage()) return;
    
    // Calculate text area position
    int textY = getContentY() + SENDER_HEIGHT + 5;
    const int TEXT_MARGIN = 10;
    
    // Clear text area (avoid clearing sender area)
    tft.fillRect(0, textY, getContentWidth(), TEXT_AREA_HEIGHT, COLOR_BLACK);
    
    // Draw visible lines for current page
    tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
    tft.setTextSize(2);
    
    int y = textY + 5; // Small top padding
    int currentPage = scrollOffset;
    int startLine = currentPage * maxVisibleLines;
    int endLine = std::min(startLine + maxVisibleLines, totalLines);
    
    for (int i = startLine; i < endLine; i++) {
        tft.setCursor(TEXT_MARGIN, y);
        tft.print(textLines[i]);
        y += LINE_HEIGHT;
    }
    
    tft.setTextSize(1);
    
    // Draw page indicator instead of scrollbar
    // if (totalLines > maxVisibleLines) {
    //     int totalPages = (totalLines + maxVisibleLines - 1) / maxVisibleLines;
    //     int currentPageNum = currentPage + 1;
        
    //     // Page indicator on the right
    //     String pageInfo = "Page " + String(currentPageNum) + "/" + String(totalPages);
    //     tft.setTextColor(COLOR_GRAY, COLOR_BLACK);
    //     tft.setCursor(getContentWidth() - 80, textY + TEXT_AREA_HEIGHT - 15);
    //     tft.print(pageInfo);
    // }
    
    LOG_INFO("📱 MessageDetailsScreen: Drew page %d, lines %d-%d of %d", 
             currentPage + 1, startLine + 1, endLine, totalLines);
}

void MessageDetailsScreen::drawTimestampSection(lgfx::LGFX_Device& tft) {
    if (!hasValidMessage()) return;
    
    // Calculate timestamp area position
    int timestampY = getContentY() + getContentHeight() - TIMESTAMP_HEIGHT;
    
    // Clear timestamp area
    tft.fillRect(0, timestampY, getContentWidth(), TIMESTAMP_HEIGHT, COLOR_BLACK);
    
    // Format and draw timestamp (matching MessagesScreen style)
    unsigned long t = currentMessage.timestamp;
    unsigned int h = (t / 3600) % 24;
    unsigned int m = (t / 60) % 60;
    unsigned int s = t % 60;
    char timebuf[32];
    snprintf(timebuf, sizeof(timebuf), "Received: %02u:%02u:%02u", h, m, s);
    
    tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
    tft.setTextSize(1);
    tft.setCursor(10, timestampY + 5);
    tft.print(timebuf);
    
    // Show page position if multiple pages
    if (totalLines > maxVisibleLines) {
        int totalPages = (totalLines + maxVisibleLines - 1) / maxVisibleLines;
        String pageInfo = String(scrollOffset + 1) + "/" + String(totalPages) + " pages";
        
        tft.setCursor(getContentWidth() - 80, timestampY + 5);
        tft.print(pageInfo);
    }
    
    LOG_INFO("📱 MessageDetailsScreen: Drew timestamp section");
}

void MessageDetailsScreen::scrollUp() {
    if (scrollOffset > 0) {
        scrollOffset--; // Move to previous page
        contentDirty = true;
        footerDirty = true;
        headerDirty = true;
        updateNavigationHints();
        forceRedraw();
        LOG_INFO("📱 MessageDetailsScreen: Scrolled to page %d", scrollOffset + 1);
    }
}

void MessageDetailsScreen::scrollDown() {
    int totalPages = (totalLines + maxVisibleLines - 1) / maxVisibleLines;
    if (scrollOffset < totalPages - 1) {
        scrollOffset++; // Move to next page
        contentDirty = true;
        footerDirty = true;
        headerDirty = true;
        updateNavigationHints();
        forceRedraw();
        LOG_INFO("📱 MessageDetailsScreen: Scrolled to page %d", scrollOffset + 1);
    }
}

void MessageDetailsScreen::updateNavigationHints() {
    navHints.clear();
    
    // Show reply button if message is valid
    if (hasValidMessage()) {
        navHints.push_back(NavHint('1', "Reply"));
    }
    
    // Show page navigation hints only if message has multiple pages
    if (hasValidMessage() && totalLines > maxVisibleLines) {
        int totalPages = (totalLines + maxVisibleLines - 1) / maxVisibleLines;
        
        if (scrollOffset > 0) {
            navHints.push_back(NavHint('2', "PgUp"));
        }
        if (scrollOffset < totalPages - 1) {
            navHints.push_back(NavHint('8', "PgDn"));
        }
    }

    // Always show back button
    navHints.push_back(NavHint('A', "Back"));
}

void MessageDetailsScreen::clearContent() {
    textLines.clear();
    scrollOffset = 0;
    totalLines = 0;
    messageSet = false;
    contentDirty = true;
    headerDirty = true;
    footerDirty = true;
    
    // Clear message data
    currentMessage = MessageInfo();
    
    updateNavigationHints();
}