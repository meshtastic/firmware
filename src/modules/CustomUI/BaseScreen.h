#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Arduino.h>
#include "UIDataState.h"

// Forward declaration
class UINavigator;

/**
 * Abstract base class for all UI screens
 * Provides structured layout with header, footer, and row-based content
 * Supports dirty rectangle updates per row for efficiency
 */
class BaseScreen {
public:
    BaseScreen(UINavigator* navigator, const char* screenName) 
        : navigator(navigator), name(screenName), needsFullRedraw(true), lastUpdateTime(0), 
          lastBatteryPercent(255), lastHasUSB(false), lastIsCharging(false),
          scrollOffset(0), totalContentRows(0), isScrollable(false), 
          currentPage(0), totalPages(1), isPaginated(false), dirtyRectCount(0) {
        for (int i = 0; i < MAX_CONTENT_ROWS; i++) {
            rowDirty[i] = false;
        }
    }
    
    virtual ~BaseScreen() = default;

    // Pure virtual methods that must be implemented by derived classes
    virtual void onEnter() = 0;                    // Called when screen becomes active
    virtual void onExit() = 0;                     // Called when screen is being left
    virtual void handleInput(uint8_t input) = 0;   // Handle button press (user button = 1, others as needed)
    virtual void draw(Adafruit_ST7789& tft, UIDataState& dataState) = 0;   // Draw screen content
    virtual bool needsUpdate(UIDataState& dataState) = 0; // Return true if screen needs redraw based on data changes

    // Layout system - header, footer, content
    void drawLayoutHeader(Adafruit_ST7789& tft, UIDataState& dataState);
    void drawLayoutFooter(Adafruit_ST7789& tft, const char* hints[], int hintCount);
    // Layout drawing methods
    void drawFullLayout(Adafruit_ST7789& tft, UIDataState& dataState, 
                       const char* hints[], int hintCount);
    void drawRowByRowLayout(Adafruit_ST7789& tft, UIDataState& dataState, 
                           const char* hints[], int hintCount);
    
    // Legacy methods (kept for compatibility, deprecated)
    virtual void drawHeader(Adafruit_ST7789& tft, const char* title = nullptr);
    virtual void drawFooter(Adafruit_ST7789& tft, const char* footerText);
    
    // Screen management
    const char* getName() const { return name; }
    void markForFullRedraw() { needsFullRedraw = true; }
    void clearRedrawFlag() { needsFullRedraw = false; }
    bool getNeedsFullRedraw() const { return needsFullRedraw; }
    
    // Row management
    void markRowDirty(int rowIndex);
    void clearAllRowsDirty();
    bool isRowDirty(int rowIndex) const;
    void markContentRowDirty(int rowIndex) { markRowDirty(rowIndex); }
    
    // Scrolling support
    void setScrollable(bool scrollable, int totalRows);
    void scrollUp();
    void scrollDown();
    int getScrollOffset() const { return scrollOffset; }
    int getVisibleRows() const { return CONTENT_ROWS; }
    
    // Page-based navigation support (automatic when content > available rows)
    void setPaginated(bool paginated, int totalRows);
    void nextPage();
    void previousPage();
    int getCurrentPage() const { return currentPage; }
    int getTotalPages() const { return totalPages; }
    bool getIsPaginated() const { return isPaginated; }
    int getPageStartRow() const; // Get starting row index for current page
    int getRowsPerPage() const; // Get actual rows per page (accounting for page info row)
    void calculatePages(int totalRows);
    
    // Legacy dirty rectangle support (deprecated)
    struct DirtyRect {
        int x, y, width, height;
        bool active;
    };
    
    void markDirtyRect(int x, int y, int width, int height);
    void clearDirtyRects();
    bool hasDirtyRects() const;

protected:
    UINavigator* navigator;
    const char* name;
    bool needsFullRedraw;
    unsigned long lastUpdateTime;
    
    // Header state tracking (common to all screens)
    uint8_t lastBatteryPercent;
    bool lastHasUSB;
    bool lastIsCharging;
    
    // Scrolling state
    int scrollOffset;
    int totalContentRows;
    bool isScrollable;
    
    // Page-based navigation (generic for all screens)
    int currentPage;
    int totalPages;
    bool isPaginated;
    static const int DEFAULT_ROWS_PER_PAGE = 10; // 11 content rows - 1 for page info
    
    // Row-based dirty tracking
    static const int MAX_CONTENT_ROWS = 20;
    bool rowDirty[MAX_CONTENT_ROWS];
    
    // Legacy dirty rectangles (deprecated)
    static const int MAX_DIRTY_RECTS = 4;
    DirtyRect dirtyRects[MAX_DIRTY_RECTS];
    int dirtyRectCount;
    
    // Helper methods for drawing
    void clearRect(Adafruit_ST7789& tft, int x, int y, int width, int height);
    void drawTextInRect(Adafruit_ST7789& tft, int x, int y, int width, int height, 
                       const char* text, uint16_t textColor, uint16_t bgColor, uint8_t textSize = 1);
    
    // Header state management (common to all screens)
    bool hasHeaderDataChanged(const UIDataState& dataState);
    
    // Page navigation helpers
    void drawPageInfo(Adafruit_ST7789& tft, int totalItems, int rowIndex = 0);
    bool shouldShowPageInfo() const { return isPaginated && totalPages > 1; }
    
    // Icon drawing helpers
    void drawBatteryIcon(Adafruit_ST7789& tft, int x, int y, uint8_t percent, bool charging);
    void drawUSBIcon(Adafruit_ST7789& tft, int x, int y);
    void drawChargingIcon(Adafruit_ST7789& tft, int x, int y);
    
    // Row helpers
    int getRowY(int rowIndex) const { return (rowIndex * ROW_HEIGHT); }
    int getContentRowY(int rowIndex) const { return getRowY(1 + rowIndex); } // +1 for header
    
    // Common colors
    static const uint16_t COLOR_TEXT = ST77XX_GREEN;           // Green text
    static const uint16_t COLOR_BORDER = 0x0320;               // Dark green for borders
    static const uint16_t COLOR_HIGHLIGHT_BG = ST77XX_GREEN;   // Green background when selected
    static const uint16_t COLOR_HIGHLIGHT_TEXT = ST77XX_WHITE; // White text when selected
    static const uint16_t COLOR_BACKGROUND = ST77XX_BLACK;
    static const uint16_t COLOR_HEADER = ST77XX_GREEN;
    static const uint16_t COLOR_ACCENT = ST77XX_CYAN;
    static const uint16_t COLOR_WARNING = ST77XX_YELLOW;
    static const uint16_t COLOR_ERROR = ST77XX_RED;
    static const uint16_t COLOR_SUCCESS = ST77XX_GREEN;        // Success/connected state
    
    // Layout constants (320x240 landscape)
    static const int SCREEN_WIDTH = 320;
    static const int SCREEN_HEIGHT = 240;
    static const int ROW_HEIGHT = 18;                          // Fixed row height
    static const int HEADER_ROW_HEIGHT = 18;                   // Header is 1 row
    static const int FOOTER_ROW_HEIGHT = 18;                   // Footer is 1 row
    static const int CONTENT_ROWS = 11;                        // (240 - 18 - 18) / 18 = 11 rows
    static const int ICON_SIZE = 14;                           // Icons fit in row with padding
    static const int BORDER_WIDTH = 2;                         // 1.5px rounded to 2
    
    // Content area bounds
    static const int CONTENT_X = BORDER_WIDTH;
    static const int CONTENT_Y = HEADER_ROW_HEIGHT;
    static const int CONTENT_WIDTH = SCREEN_WIDTH - (2 * BORDER_WIDTH);
    static const int CONTENT_HEIGHT = CONTENT_ROWS * ROW_HEIGHT;
    
    // Legacy constants (deprecated - for backward compatibility)
    static const int CONTENT_START_Y = HEADER_ROW_HEIGHT;      // Use CONTENT_Y instead
    static const int HEADER_HEIGHT = HEADER_ROW_HEIGHT;        // Use HEADER_ROW_HEIGHT instead
    static const int FOOTER_HEIGHT = FOOTER_ROW_HEIGHT;        // Use FOOTER_ROW_HEIGHT instead
    
    // Update intervals
    static const unsigned long MIN_UPDATE_INTERVAL = 1000; // Minimum 1 second between updates
};