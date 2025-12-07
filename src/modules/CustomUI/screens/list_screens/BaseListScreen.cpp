#include "BaseListScreen.h"
#include <algorithm>

// Logging macro
#ifndef LOG_INFO
#define LOG_INFO(format, ...) Serial.printf("[INFO] " format "\n", ##__VA_ARGS__)
#endif

BaseListScreen::BaseListScreen(const String& screenName, int itemHeight) 
    : BaseScreen(screenName), itemHeight(itemHeight) {
    
    // Initialize list state
    selectedIndex = 0;
    scrollOffset = 0;
    lastSelectedIndex = -1;
    selectionChanged = false;
    needsListRedraw = true;
    needsScrollUpdate = true;
    
    // Calculate layout
    listStartY = getContentY() + 5; // Small padding from content area
    listHeight = getContentHeight() - 10; // Leave padding at bottom
    maxVisibleItems = listHeight / itemHeight;
    
    LOG_INFO("🔧 BaseListScreen '%s': Created (itemHeight=%d, maxVisible=%d)", 
        screenName.c_str(), itemHeight, maxVisibleItems);
}

BaseListScreen::~BaseListScreen() {
    LOG_INFO("🔧 BaseListScreen '%s': Destroyed", name.c_str());
}

void BaseListScreen::onEnter() {
    LOG_INFO("🔧 BaseListScreen '%s': Entering screen", name.c_str());
    
    // Reset list state for fresh entry
    selectedIndex = 0;
    scrollOffset = 0;
    lastSelectedIndex = -1;
    selectionChanged = false;
    needsListRedraw = true;
    needsScrollUpdate = true;
    
    // Force redraw to show screen immediately
    forceRedraw();
}

void BaseListScreen::onExit() {
    LOG_INFO("🔧 BaseListScreen '%s': Exiting screen", name.c_str());
    
    // Reset state
    selectedIndex = 0;
    scrollOffset = 0;
    lastSelectedIndex = -1;
    selectionChanged = false;
    needsListRedraw = false;
    needsScrollUpdate = false;
}

void BaseListScreen::onDraw(lgfx::LGFX_Device& tft) {
    // Let derived class prepare data or show loading states
    onBeforeDrawItems(tft);
    
    int itemCount = getItemCount();
    
    // Handle empty list case
    if (itemCount == 0) {
        // Clear list area
        tft.fillRect(0, getContentY(), getContentWidth(), getContentHeight(), COLOR_BLACK);
        needsListRedraw = false;
        needsScrollUpdate = false;
        return;
    }
    
    // Ensure selection is within bounds
    if (selectedIndex >= itemCount) {
        selectedIndex = std::max(0, itemCount - 1);
        selectionChanged = true;
    }
    
    // Update scroll offset if needed
    if (needsScrollUpdate || selectionChanged) {
        updateScrollOffset();
        needsScrollUpdate = false;
    }
    
    // Optimized redraw logic
    if (needsListRedraw) {
        // Full list redraw needed
        LOG_INFO("🔧 BaseListScreen: Full list redraw (scrollOffset: %d)", scrollOffset);
        drawFullList(tft);
        drawScrollIndicators(tft);
        needsListRedraw = false;
        selectionChanged = false;
        lastSelectedIndex = selectedIndex;
        
    } else if (selectionChanged) {
        // Only selection changed - use dirty rectangles
        LOG_INFO("🔧 BaseListScreen: Dirty rectangle update (%d -> %d)", lastSelectedIndex, selectedIndex);
        
        // Clear old selection highlight
        if (lastSelectedIndex >= 0 && lastSelectedIndex != selectedIndex) {
            drawSelectionHighlight(tft, lastSelectedIndex, false);
            
            // Redraw the previously selected item without highlight
            int oldY = getItemY(lastSelectedIndex);
            if (oldY >= 0) {
                drawItem(tft, lastSelectedIndex, oldY, false);
            }
        }
        
        // Draw new selection highlight and item
        drawSelectionHighlight(tft, selectedIndex, true);
        int newY = getItemY(selectedIndex);
        if (newY >= 0) {
            drawItem(tft, selectedIndex, newY, true);
        }
        
        // Update scroll indicators if needed
        drawScrollIndicators(tft);
        
        selectionChanged = false;
        lastSelectedIndex = selectedIndex;
    }
}

bool BaseListScreen::handleKeyPress(char key) {
    int itemCount = getItemCount();
    
    LOG_INFO("🔧 BaseListScreen: handleKeyPress '%c' (itemCount: %d)", key, itemCount);
    
    if (itemCount == 0) {
        LOG_INFO("🔧 BaseListScreen: No items to navigate");
        return false; // No items to navigate
    }
    
    switch (key) {
        case '2': // Up arrow
            LOG_INFO("🔧 BaseListScreen: Scroll up - current: %d", selectedIndex);
            scrollUp();
            return true;
            
        case '8': // Down arrow  
            LOG_INFO("🔧 BaseListScreen: Scroll down - current: %d", selectedIndex);
            scrollDown();
            return true;
            
        case '1': // Select
            LOG_INFO("🔧 BaseListScreen: Item selected: %d", selectedIndex);
            onItemSelected(selectedIndex);
            return true;
            
        default:
            // Let derived classes handle other keys
            LOG_INFO("🔧 BaseListScreen: Key '%c' not handled, returning false", key);
            return false;
    }
}

void BaseListScreen::scrollUp() {
    if (selectedIndex > 0) {
        // Check if we're at the first visible item and need to scroll to previous page
        if (selectedIndex == scrollOffset && scrollOffset > 0) {
            // Page up: move to previous page and select last item
            scrollOffset = std::max(0, scrollOffset - maxVisibleItems);
            selectedIndex = std::min(scrollOffset + maxVisibleItems - 1, getItemCount() - 1);
            needsListRedraw = true;
            LOG_INFO("🔧 BaseListScreen: Page up - scrollOffset: %d, selectedIndex: %d", scrollOffset, selectedIndex);
        } else {
            // Normal scroll within current page
            selectedIndex--;
            LOG_INFO("🔧 BaseListScreen: scrollUp - new selectedIndex: %d", selectedIndex);
        }
        selectionChanged = true;
    }
}

void BaseListScreen::scrollDown() {
    int itemCount = getItemCount();
    if (selectedIndex < itemCount - 1) {
        // Check if we're at the last visible item and need to scroll to next page
        if (selectedIndex == scrollOffset + maxVisibleItems - 1 && 
            selectedIndex < itemCount - 1) {
            // Page down: move to next page and select first item
            scrollOffset = std::min(scrollOffset + maxVisibleItems, itemCount - maxVisibleItems);
            selectedIndex = scrollOffset;
            needsListRedraw = true;
            LOG_INFO("🔧 BaseListScreen: Page down - scrollOffset: %d, selectedIndex: %d", scrollOffset, selectedIndex);
        } else {
            // Normal scroll within current page
            selectedIndex++;
            LOG_INFO("🔧 BaseListScreen: scrollDown - new selectedIndex: %d", selectedIndex);
        }
        selectionChanged = true;
    }
}

void BaseListScreen::setSelection(int index) {
    int itemCount = getItemCount();
    if (index >= 0 && index < itemCount && index != selectedIndex) {
        selectedIndex = index;
        selectionChanged = true;
        needsScrollUpdate = true;
        LOG_INFO("🔧 BaseListScreen: Selection set to: %d", selectedIndex);
    }
}

void BaseListScreen::updateScrollOffset() {
    int itemCount = getItemCount();
    
    // Simple bounds checking - pagination is handled in scrollUp/scrollDown
    // This method is only called for programmatic setSelection()
    if (selectedIndex < scrollOffset) {
        scrollOffset = selectedIndex;
        needsListRedraw = true;
        LOG_INFO("🔧 BaseListScreen: Adjust scroll for selection %d (scrollOffset: %d)", selectedIndex, scrollOffset);
    } else if (selectedIndex >= scrollOffset + maxVisibleItems) {
        scrollOffset = selectedIndex - maxVisibleItems + 1;
        needsListRedraw = true;
        LOG_INFO("🔧 BaseListScreen: Adjust scroll for selection %d (scrollOffset: %d)", selectedIndex, scrollOffset);
    }
    
    // Ensure scroll offset is within bounds
    scrollOffset = std::max(0, std::min(scrollOffset, itemCount - maxVisibleItems));
    if (scrollOffset < 0) scrollOffset = 0;
}

void BaseListScreen::drawFullList(lgfx::LGFX_Device& tft) {
    // Clear entire content area including scrollbar
    tft.fillRect(0, getContentY(), SCREEN_WIDTH, getContentHeight(), COLOR_BLACK);
    
    int itemCount = getItemCount();
    if (itemCount == 0) return;
    
    // Calculate visible range
    int endIndex = std::min(itemCount, scrollOffset + maxVisibleItems);
    int y = listStartY;
    
    // Draw all visible items
    for (int i = scrollOffset; i < endIndex; i++) {
        bool isSelected = (i == selectedIndex);
        
        // Draw selection highlight first if selected
        if (isSelected) {
            drawSelectionHighlight(tft, i, true);
        }
        
        // Draw the item
        drawItem(tft, i, y, isSelected);
        y += itemHeight;
    }
}

void BaseListScreen::drawSelectionHighlight(lgfx::LGFX_Device& tft, int index, bool highlight) {
    int y = getItemY(index);
    if (y < 0) return; // Item not visible
    
    uint16_t color = highlight ? COLOR_SELECTION : COLOR_BLACK;
    
    // Draw highlight rectangle only in content area (not over scrollbar)
    tft.fillRect(0, y, getContentWidth(), itemHeight, color);
}

int BaseListScreen::getItemY(int index) {
    // Check if item is visible
    if (index < scrollOffset || index >= scrollOffset + maxVisibleItems) {
        return -1; // Not visible
    }
    
    // Calculate Y position
    int visibleIndex = index - scrollOffset;
    return listStartY + (visibleIndex * itemHeight);
}

void BaseListScreen::drawScrollIndicators(lgfx::LGFX_Device& tft) {
    int itemCount = getItemCount();
    if (itemCount <= maxVisibleItems) {
        return; // No scrolling needed
    }
    
    // Scrollbar area (right side)
    int scrollbarX = getContentWidth();
    int scrollbarY = getContentY();
    int scrollbarHeight = getContentHeight();
    int scrollbarAreaWidth = SCROLLBAR_WIDTH;
    
    // Clear scrollbar area
    tft.fillRect(scrollbarX, scrollbarY, scrollbarAreaWidth, scrollbarHeight, COLOR_BLACK);
    
    // Draw scrollbar track (thin vertical line)
    int trackX = scrollbarX + 4;
    int trackWidth = 2;
    tft.fillRect(trackX, scrollbarY + 2, trackWidth, scrollbarHeight - 4, 0x2104); // Dark green track
    
    // Calculate and draw scroll thumb
    if (itemCount > maxVisibleItems) {
        int thumbHeight = std::max(8, (scrollbarHeight * maxVisibleItems) / itemCount);
        int maxThumbY = scrollbarY + scrollbarHeight - thumbHeight - 2;
        int thumbY = scrollbarY + 2 + ((maxThumbY - scrollbarY - 2) * scrollOffset) / std::max(1, itemCount - maxVisibleItems);
        
        // Draw scroll thumb (bright green, slightly wider than track)
        int thumbX = scrollbarX + 3;
        int thumbWidth = 4;
        tft.fillRect(thumbX, thumbY, thumbWidth, thumbHeight, 0x07E0); // Bright green thumb
        
        // Add small border to thumb for better visibility
        tft.drawRect(thumbX, thumbY, thumbWidth, thumbHeight, 0xFFFF); // White border
    }
}