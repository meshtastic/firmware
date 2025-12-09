#pragma once

#include "../BaseScreen.h"
#include <vector>

/**
 * Abstract base class for list-based screens in CustomUI
 * Provides common list functionality: selection, scrolling, dirty rectangle optimization
 * 
 * Features:
 * - Generic list navigation (up/down arrow keys)
 * - Automatic scrolling to keep selection visible
 * - Dirty rectangle optimization for selection highlighting
 * - Abstract methods for item-specific rendering
 * - Configurable item height and visible count
 * 
 * Derived classes must implement:
 * - drawItem(): Render individual list items
 * - getItemCount(): Return total number of items
 * - onItemSelected(): Handle item selection (optional)
 */
class BaseListScreen : public BaseScreen {
public:
    BaseListScreen(const String& screenName, int itemHeight = 20);
    virtual ~BaseListScreen();

    // BaseScreen interface
    virtual void onEnter() override;
    virtual void onExit() override;
    virtual void onDraw(lgfx::LGFX_Device& tft) override;
    virtual bool handleKeyPress(char key) override;
    virtual bool needsUpdate() const override { 
        return BaseScreen::needsUpdate() || selectionChanged || needsListRedraw || needsScrollUpdate; 
    }

protected:
    // Abstract methods for derived classes
    /**
     * Draw individual list item
     * @param tft Display device
     * @param index Item index in list
     * @param y Y position to draw at
     * @param isSelected Whether this item is currently selected
     */
    virtual void drawItem(lgfx::LGFX_Device& tft, int index, int y, bool isSelected) = 0;
    
    /**
     * Get total number of items in list
     * @return Total item count
     */
    virtual int getItemCount() = 0;
    
    /**
     * Called when an item is selected (optional override)
     * @param index Selected item index
     */
    virtual void onItemSelected(int index) {}
    
    /**
     * Called before drawing list items (optional override)
     * Useful for refreshing data, showing loading states, etc.
     * @param tft Display device
     * @return true if derived class handled all drawing (skip normal list drawing), false to proceed with normal list
     */
    virtual bool onBeforeDrawItems(lgfx::LGFX_Device& tft) { return false; }

    // List navigation
    void scrollUp();
    void scrollDown();
    void setSelection(int index);
    
    // Accessors
    int getSelectedIndex() const { return selectedIndex; }
    int getScrollOffset() const { return scrollOffset; }
    int getMaxVisibleItems() const { return maxVisibleItems; }
    
    // Override content width to account for scrollbar
    int getContentWidth() const { return CONTENT_WIDTH; }
    
    // Force list area redraw
    void invalidateList() { needsListRedraw = true; }
    void invalidateSelection() { selectionChanged = true; }

private:
    /**
     * Update scroll offset to keep selection visible
     */
    void updateScrollOffset();
    
    /**
     * Draw the complete list with all visible items
     */
    void drawFullList(lgfx::LGFX_Device& tft);
    
    /**
     * Draw selection highlight rectangle
     * @param tft Display device
     * @param index Item index to highlight
     * @param highlight True to draw highlight, false to clear
     */
    void drawSelectionHighlight(lgfx::LGFX_Device& tft, int index, bool highlight);
    
    /**
     * Get Y position for item at given index
     * @param index Item index
     * @return Y coordinate, or -1 if not visible
     */
    int getItemY(int index);
    
    /**
     * Draw scroll indicators if needed
     */
    void drawScrollIndicators(lgfx::LGFX_Device& tft);

protected:
    // List state
    int selectedIndex;          // Currently selected item (0-based)
    int scrollOffset;           // First visible item index
    int maxVisibleItems;        // Maximum items visible at once
    int itemHeight;             // Height of each list item in pixels
    
    // Dirty rectangle optimization
    int lastSelectedIndex;      // Previous selection for dirty rect
    bool selectionChanged;      // True if selection changed since last draw
    bool needsListRedraw;       // True if entire list needs redraw
    bool needsScrollUpdate;     // True if scroll indicators need update
    
    // Layout
    int listStartY;             // Y coordinate where list starts
    int listHeight;             // Total height available for list
    
    // Colors for list rendering
    static const uint16_t COLOR_BLACK = 0x0000;
    static const uint16_t COLOR_SELECTION = 0x4208;     // Dim green for selection
    static const uint16_t COLOR_SCROLL_INDICATOR = 0x4208; // Dim green for scroll arrows
    
    // Layout constants for scrollbar
    static const int SCROLLBAR_WIDTH = 12; // Width reserved for scrollbar
    static const int CONTENT_WIDTH = 320 - SCROLLBAR_WIDTH; // Content area width
};