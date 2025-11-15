#include "BaseScreen.h"
#include "UINavigator.h"
#include "configuration.h"
#include <Arduino.h>

// ===== NEW LAYOUT SYSTEM =====

void BaseScreen::drawLayoutHeader(Adafruit_ST7789& tft, UIDataState& dataState) {
    // Only redraw header if data has actually changed
    if (!hasHeaderDataChanged(dataState)) {
        return; // Skip redraw if nothing changed
    }
    
    const auto& systemData = dataState.getSystemData();
    
    // Draw header border
    tft.drawRect(0, 0, SCREEN_WIDTH, HEADER_ROW_HEIGHT, COLOR_BORDER);
    tft.fillRect(BORDER_WIDTH, BORDER_WIDTH, SCREEN_WIDTH - (2 * BORDER_WIDTH), 
                 HEADER_ROW_HEIGHT - (2 * BORDER_WIDTH), COLOR_BACKGROUND);
    
    // Draw device long name (left side)
    tft.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    tft.setTextSize(1);
    tft.setCursor(4, 5);
    tft.print(systemData.longName);
    
    // Right side: Icons and battery info based on power state
    int rightX = SCREEN_WIDTH - 4;
    
    if (systemData.hasUSB && systemData.isCharging) {
        // Condition 1: USB + charging -> show battery icon + percentage
        char battStr[8];
        snprintf(battStr, sizeof(battStr), "%d%%", systemData.batteryPercent);
        int textWidth = strlen(battStr) * 6;
        rightX -= textWidth;
        tft.setCursor(rightX, 5);
        tft.print(battStr);
        rightX -= 2;
        
        // Battery icon with charging indicator
        rightX -= ICON_SIZE;
        drawBatteryIcon(tft, rightX, 2, systemData.batteryPercent, true);
        rightX -= 2;
        
    } else if (systemData.hasUSB && !systemData.isCharging) {
        // Condition 2: USB + not charging -> show USB icon only
        rightX -= ICON_SIZE;
        drawUSBIcon(tft, rightX, 2);
        
    } else if (!systemData.hasUSB && systemData.hasBattery) {
        // Condition 3: No USB + battery -> show percentage only
        char battStr[8];
        snprintf(battStr, sizeof(battStr), "%d%%", systemData.batteryPercent);
        int textWidth = strlen(battStr) * 6;
        rightX -= textWidth;
        tft.setCursor(rightX, 5);
        tft.print(battStr);
    }
}

void BaseScreen::drawLayoutFooter(Adafruit_ST7789& tft, const char* hints[], int hintCount) {
    int footerY = SCREEN_HEIGHT - FOOTER_ROW_HEIGHT;
    
    // Draw footer border
    tft.drawRect(0, footerY, SCREEN_WIDTH, FOOTER_ROW_HEIGHT, COLOR_BORDER);
    tft.fillRect(BORDER_WIDTH, footerY + BORDER_WIDTH, 
                 SCREEN_WIDTH - (2 * BORDER_WIDTH), 
                 FOOTER_ROW_HEIGHT - (2 * BORDER_WIDTH), COLOR_BACKGROUND);
    
    if (hintCount == 0) return;
    
    // For scrollable lists, show special scroll indicators
    if (isScrollable && hintCount == 2) {
        // "2:Up 3:Down" for scrolling
        tft.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
        tft.setTextSize(1);
        
        int spacing = SCREEN_WIDTH / 2;
        tft.setCursor(spacing / 2 - 18, footerY + 5);
        tft.print(hints[0]); // "2:Up"
        
        tft.setCursor(SCREEN_WIDTH / 2 + spacing / 2 - 24, footerY + 5);
        tft.print(hints[1]); // "3:Down"
        return;
    }
    
    // Regular navigation hints (up to 4 items)
    int itemWidth = SCREEN_WIDTH / hintCount;
    tft.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    tft.setTextSize(1);
    
    for (int i = 0; i < hintCount && i < 4; i++) {
        int textWidth = strlen(hints[i]) * 6;
        int centerX = (itemWidth * i) + (itemWidth / 2) - (textWidth / 2);
        tft.setCursor(centerX, footerY + 5);
        tft.print(hints[i]);
    }
}

void BaseScreen::drawFullLayout(Adafruit_ST7789& tft, UIDataState& dataState, 
                                const char* hints[], int hintCount) {
    // Draw outer border
    tft.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BORDER);
    
    // Draw header
    drawLayoutHeader(tft, dataState);
    
    // Draw content area border
    int contentY = HEADER_ROW_HEIGHT;
    int contentHeight = SCREEN_HEIGHT - HEADER_ROW_HEIGHT - FOOTER_ROW_HEIGHT;
    tft.drawRect(0, contentY, SCREEN_WIDTH, contentHeight, COLOR_BORDER);
    
    // Clear content area
    tft.fillRect(BORDER_WIDTH, contentY + BORDER_WIDTH, 
                 SCREEN_WIDTH - (2 * BORDER_WIDTH), 
                 contentHeight - (2 * BORDER_WIDTH), COLOR_BACKGROUND);
    
    // Draw footer
    drawLayoutFooter(tft, hints, hintCount);
}

void BaseScreen::drawRowByRowLayout(Adafruit_ST7789& tft, UIDataState& dataState, 
                                    const char* hints[], int hintCount) {
    bool needsFullLayout = getNeedsFullRedraw();
    bool needsHeaderUpdate = hasHeaderDataChanged(dataState);
    bool needsContentUpdate = hasDirtyRects();
    
    if (needsFullLayout) {
        // First time or major layout change - draw everything
        drawFullLayout(tft, dataState, hints, hintCount);
        clearAllRowsDirty();
        clearRedrawFlag();
        clearDirtyRects();
    } else {
        // Incremental updates only
        if (needsHeaderUpdate) {
            drawLayoutHeader(tft, dataState);
        }
        
        if (needsContentUpdate) {
            // Row-by-row content update - only clear and redraw what's needed
            for (int row = 0; row < CONTENT_ROWS; row++) {
                if (isRowDirty(row) || needsContentUpdate) {
                    // Clear this specific row
                    int y = CONTENT_Y + BORDER_WIDTH + (row * ROW_HEIGHT);
                    clearRect(tft, CONTENT_X + BORDER_WIDTH, y, 
                             CONTENT_WIDTH - (2 * BORDER_WIDTH), ROW_HEIGHT);
                    
                    // Mark row as clean (content will be drawn by derived class)
                    rowDirty[row] = false;
                }
            }
            clearDirtyRects();
        }
        
        // Update footer if hints changed (for pagination)
        if (needsContentUpdate) {
            drawLayoutFooter(tft, hints, hintCount);
        }
    }
}

// ===== LEGACY METHODS (Deprecated) =====

void BaseScreen::drawHeader(Adafruit_ST7789& tft, const char* title) {
    // Legacy method - kept for compatibility
    tft.fillRect(0, 0, SCREEN_WIDTH, HEADER_ROW_HEIGHT, COLOR_HEADER);
    tft.setTextColor(COLOR_BACKGROUND, COLOR_HEADER);
    tft.setTextSize(1);
    tft.setCursor(5, 5);
    tft.print(title ? title : name);
}

bool BaseScreen::hasHeaderDataChanged(const UIDataState& dataState) {
    const auto& systemData = dataState.getSystemData();
    
    bool changed = (systemData.batteryPercent != lastBatteryPercent ||
                   systemData.hasUSB != lastHasUSB ||
                   systemData.isCharging != lastIsCharging);
    
    if (changed) {
        lastBatteryPercent = systemData.batteryPercent;
        lastHasUSB = systemData.hasUSB;
        lastIsCharging = systemData.isCharging;
    }
    
    return changed;
}

// ===== PAGE NAVIGATION SUPPORT =====

void BaseScreen::setPaginated(bool paginated, int totalRows) {
    isPaginated = paginated;
    totalContentRows = totalRows;
    
    if (paginated) {
        calculatePages(totalRows);
        LOG_DEBUG("🔧 UI: Pagination enabled: %d items, %d pages, current page %d", 
                 totalRows, totalPages, currentPage + 1);
    } else {
        currentPage = 0;
        totalPages = 1;
        LOG_DEBUG("🔧 UI: Pagination disabled: %d items fit in available space", totalRows);
    }
}

void BaseScreen::nextPage() {
    if (!isPaginated) {
        LOG_DEBUG("🔧 UI: nextPage called but not paginated");
        return;
    }
    
    if (currentPage >= totalPages - 1) {
        LOG_DEBUG("🔧 UI: nextPage called but already on last page (%d/%d)", currentPage + 1, totalPages);
        return;
    }
    
    currentPage++;
    LOG_DEBUG("🔧 UI: Next page: %d/%d", currentPage + 1, totalPages);
    markDirtyRect(CONTENT_X, CONTENT_Y, CONTENT_WIDTH, CONTENT_HEIGHT);
    // Mark all content rows as dirty for efficient row-by-row update
    for (int i = 0; i < CONTENT_ROWS; i++) {
        markRowDirty(i);
    }
}

void BaseScreen::previousPage() {
    if (!isPaginated) {
        LOG_DEBUG("🔧 UI: previousPage called but not paginated");
        return;
    }
    
    if (currentPage <= 0) {
        LOG_DEBUG("🔧 UI: previousPage called but already on first page (%d/%d)", currentPage + 1, totalPages);
        return;
    }
    
    currentPage--;
    LOG_DEBUG("🔧 UI: Previous page: %d/%d", currentPage + 1, totalPages);
    markDirtyRect(CONTENT_X, CONTENT_Y, CONTENT_WIDTH, CONTENT_HEIGHT);
    // Mark all content rows as dirty for efficient row-by-row update
    for (int i = 0; i < CONTENT_ROWS; i++) {
        markRowDirty(i);
    }
}

int BaseScreen::getPageStartRow() const {
    if (!isPaginated) return 0;
    
    int rowsPerPage = getRowsPerPage();
    return currentPage * rowsPerPage;
}

int BaseScreen::getRowsPerPage() const {
    if (!isPaginated || totalPages <= 1) {
        return CONTENT_ROWS;
    }
    return DEFAULT_ROWS_PER_PAGE; // Reserve 1 row for page info (10 out of 11)
}

void BaseScreen::calculatePages(int totalRows) {
    if (totalRows <= 0) {
        totalPages = 1;
        currentPage = 0;
        LOG_DEBUG("🔧 UI: No items to paginate");
        return;
    }
    
    if (totalRows <= CONTENT_ROWS) {
        totalPages = 1;
        currentPage = 0;
        LOG_DEBUG("🔧 UI: All %d items fit in %d rows, no pagination needed", totalRows, CONTENT_ROWS);
        return;
    }
    
    int rowsPerPage = DEFAULT_ROWS_PER_PAGE; // 10 rows per page to reserve 1 for page info
    totalPages = (totalRows + rowsPerPage - 1) / rowsPerPage;
    
    // Ensure current page is within bounds
    if (currentPage >= totalPages) {
        currentPage = totalPages - 1;
    }
    if (currentPage < 0) {
        currentPage = 0;
    }
    
    LOG_DEBUG("🔧 UI: Calculated %d pages for %d items (%d per page), current page %d", 
             totalPages, totalRows, rowsPerPage, currentPage + 1);
}

void BaseScreen::drawPageInfo(Adafruit_ST7789& tft, int totalItems, int rowIndex) {
    if (!shouldShowPageInfo()) return;
    
    char pageInfo[48];
    snprintf(pageInfo, sizeof(pageInfo), "Page %d/%d (%d items)", 
             currentPage + 1, totalPages, totalItems);
    
    int y = CONTENT_Y + BORDER_WIDTH + (rowIndex * ROW_HEIGHT);
    drawTextInRect(tft, CONTENT_X + 4, y, CONTENT_WIDTH - 8, ROW_HEIGHT,
                   pageInfo, COLOR_ACCENT, COLOR_BACKGROUND);
}

void BaseScreen::drawFooter(Adafruit_ST7789& tft, const char* footerText) {
    int footerY = SCREEN_HEIGHT - FOOTER_ROW_HEIGHT;
    tft.fillRect(0, footerY, SCREEN_WIDTH, FOOTER_ROW_HEIGHT, COLOR_BACKGROUND);
    tft.setTextColor(COLOR_HEADER, COLOR_BACKGROUND);
    tft.setTextSize(1);
    int textWidth = strlen(footerText) * 6;
    int centerX = (SCREEN_WIDTH - textWidth) / 2;
    tft.setCursor(centerX, footerY + 5);
    tft.print(footerText);
}

// ===== ICON DRAWING =====

void BaseScreen::drawBatteryIcon(Adafruit_ST7789& tft, int x, int y, uint8_t percent, bool charging) {
    // Battery outline (12x8 at 14px icon size)
    tft.drawRect(x, y + 3, 10, 8, COLOR_TEXT);
    tft.fillRect(x + 10, y + 5, 2, 4, COLOR_TEXT); // Battery terminal
    
    // Fill level based on percentage
    int fillWidth = (percent * 8) / 100;
    if (fillWidth > 0) {
        tft.fillRect(x + 1, y + 4, fillWidth, 6, COLOR_TEXT);
    }
    
    // Charging bolt overlay
    if (charging) {
        tft.fillRect(x + 3, y + 5, 4, 6, COLOR_BACKGROUND); // Clear center
        // Draw lightning bolt
        tft.drawLine(x + 6, y + 5, x + 4, y + 8, COLOR_WARNING);
        tft.drawLine(x + 4, y + 8, x + 6, y + 10, COLOR_WARNING);
    }
}

void BaseScreen::drawUSBIcon(Adafruit_ST7789& tft, int x, int y) {
    // Simple USB symbol (12x12)
    tft.drawLine(x + 6, y + 2, x + 6, y + 10, COLOR_TEXT);  // Vertical line
    tft.drawLine(x + 6, y + 2, x + 3, y + 5, COLOR_TEXT);   // Left branch
    tft.drawLine(x + 6, y + 2, x + 9, y + 5, COLOR_TEXT);   // Right branch
    tft.fillCircle(x + 3, y + 6, 1, COLOR_TEXT);            // Left circle
    tft.fillRect(x + 8, y + 5, 2, 2, COLOR_TEXT);           // Right square
    tft.fillCircle(x + 6, y + 11, 1, COLOR_TEXT);           // Bottom circle
}

void BaseScreen::drawChargingIcon(Adafruit_ST7789& tft, int x, int y) {
    // Lightning bolt (10x12)
    tft.drawLine(x + 6, y + 1, x + 4, y + 6, COLOR_WARNING);
    tft.drawLine(x + 4, y + 6, x + 6, y + 6, COLOR_WARNING);
    tft.drawLine(x + 6, y + 6, x + 4, y + 11, COLOR_WARNING);
    tft.drawLine(x + 4, y + 6, x + 6, y + 1, COLOR_WARNING);
    tft.drawLine(x + 6, y + 6, x + 4, y + 11, COLOR_WARNING);
}

// ===== ROW MANAGEMENT =====

void BaseScreen::markRowDirty(int rowIndex) {
    if (rowIndex >= 0 && rowIndex < MAX_CONTENT_ROWS) {
        rowDirty[rowIndex] = true;
    }
}

void BaseScreen::clearAllRowsDirty() {
    for (int i = 0; i < MAX_CONTENT_ROWS; i++) {
        rowDirty[i] = false;
    }
}

bool BaseScreen::isRowDirty(int rowIndex) const {
    if (rowIndex >= 0 && rowIndex < MAX_CONTENT_ROWS) {
        return rowDirty[rowIndex];
    }
    return false;
}

// ===== SCROLLING SUPPORT =====

void BaseScreen::setScrollable(bool scrollable, int totalRows) {
    isScrollable = scrollable;
    totalContentRows = totalRows;
    scrollOffset = 0;
}

void BaseScreen::scrollUp() {
    if (scrollOffset > 0) {
        scrollOffset--;
        markForFullRedraw();
    }
}

void BaseScreen::scrollDown() {
    int maxOffset = totalContentRows - CONTENT_ROWS;
    if (maxOffset < 0) maxOffset = 0;
    
    if (scrollOffset < maxOffset) {
        scrollOffset++;
        markForFullRedraw();
    }
}

// ===== LEGACY DIRTY RECTANGLE SUPPORT =====

void BaseScreen::markDirtyRect(int x, int y, int width, int height) {
    if (dirtyRectCount < MAX_DIRTY_RECTS) {
        dirtyRects[dirtyRectCount] = {x, y, width, height, true};
        dirtyRectCount++;
    } else {
        markForFullRedraw();
    }
}

void BaseScreen::clearDirtyRects() {
    dirtyRectCount = 0;
    for (int i = 0; i < MAX_DIRTY_RECTS; i++) {
        dirtyRects[i].active = false;
    }
}

bool BaseScreen::hasDirtyRects() const {
    return dirtyRectCount > 0;
}

void BaseScreen::clearRect(Adafruit_ST7789& tft, int x, int y, int width, int height) {
    tft.fillRect(x, y, width, height, COLOR_BACKGROUND);
}

void BaseScreen::drawTextInRect(Adafruit_ST7789& tft, int x, int y, int width, int height, 
                               const char* text, uint16_t textColor, uint16_t bgColor, uint8_t textSize) {
    tft.fillRect(x, y, width, height, bgColor);
    tft.setTextColor(textColor, bgColor);
    tft.setTextSize(textSize);
    tft.setCursor(x, y);
    tft.print(text);
}