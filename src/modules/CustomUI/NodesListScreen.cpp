#include "NodesListScreen.h"
#include "UINavigator.h"
#include <Arduino.h>

// Use same constant as BaseScreen for consistency
static const int DEFAULT_ROWS_PER_PAGE = 10;

NodesListScreen::NodesListScreen(UINavigator* navigator) 
    : BaseScreen(navigator, "NODES") {
}

void NodesListScreen::onEnter() {
    LOG_INFO("🔧 UI: Entering Nodes List Screen");
    // Don't force full redraw on enter - let the system decide what needs updating
    // markForFullRedraw(); // This was causing unnecessary header refresh
}

void NodesListScreen::onExit() {
    LOG_INFO("🔧 UI: Exiting Nodes List Screen");
}

void NodesListScreen::handleInput(uint8_t input) {
    LOG_INFO("🔧 NodesListScreen: handleInput called with: %d", input);
    LOG_INFO("🔧 NodesListScreen: isPaginated=%d, currentPage=%d, totalPages=%d", 
             getIsPaginated(), getCurrentPage(), getTotalPages());
    
    switch (input) {
        case 1: // User button - go back to home
            navigator->navigateBack();
            break;
        case 2: // Previous page
            LOG_INFO("🔧 NodesListScreen: Key 2 pressed");
            if (getIsPaginated()) {
                LOG_INFO("🔧 NodesListScreen: Calling previousPage()");
                previousPage();
                // Force content area redraw (but not header/footer)
                markDirtyRect(CONTENT_X, CONTENT_Y, CONTENT_WIDTH, CONTENT_HEIGHT);
            } else {
                LOG_INFO("🔧 NodesListScreen: Not paginated, no action");
            }
            break;
        case 3: // Next page
            LOG_INFO("🔧 NodesListScreen: Key 3 pressed");
            if (getIsPaginated()) {
                LOG_INFO("🔧 NodesListScreen: Calling nextPage()");
                nextPage();
                // Force content area redraw (but not header/footer)
                markDirtyRect(CONTENT_X, CONTENT_Y, CONTENT_WIDTH, CONTENT_HEIGHT);
            } else {
                LOG_INFO("🔧 NodesListScreen: Not paginated, no action");
            }
            break;
        default:
            LOG_DEBUG("🔧 UI: Unhandled input: %d", input);
            break;
    }
}

bool NodesListScreen::needsUpdate(UIDataState& dataState) {
    // Update when node data changes OR when we have dirty rectangles (e.g., page navigation)
    return dataState.isNodesDataChanged() || hasDirtyRects();
}

void NodesListScreen::draw(Adafruit_ST7789& tft, UIDataState& dataState) {
    const auto& nodesData = dataState.getNodesData();
    
    // Debug logging
    LOG_INFO("🔧 NodesListScreen: draw() - nodeCount=%d, filteredNodeCount=%d, CONTENT_ROWS=%d", 
             nodesData.nodeCount, nodesData.filteredNodeCount, CONTENT_ROWS);
    
    // Set up pagination based on filtered node count
    // Use DEFAULT_ROWS_PER_PAGE (10) as threshold since we reserve 1 row for page info when paginated
    bool needsPagination = nodesData.filteredNodeCount > DEFAULT_ROWS_PER_PAGE;
    setPaginated(needsPagination, nodesData.filteredNodeCount);
    
    LOG_INFO("🔧 NodesListScreen: After setPaginated - isPaginated=%d, totalPages=%d, currentPage=%d",
             getIsPaginated(), getTotalPages(), getCurrentPage());
    
    bool needsFullLayout = getNeedsFullRedraw();
    bool needsHeaderUpdate = hasHeaderDataChanged(dataState);
    bool needsContentUpdate = hasDirtyRects() || dataState.isNodesDataChanged();
    
    // Mark all content rows as dirty if node data changed (for row-by-row updates)
    if (dataState.isNodesDataChanged()) {
        for (int i = 0; i < CONTENT_ROWS; i++) {
            markContentRowDirty(i);
        }
    }
    
    // Prepare navigation hints
    const char* hints[4] = {"A:Back", nullptr, nullptr, nullptr};
    int hintCount = 1;
    
    if (getIsPaginated() && getTotalPages() > 1) {
        if (getCurrentPage() > 0) {
            hints[hintCount++] = "2:Prev";
        }
        if (getCurrentPage() < getTotalPages() - 1) {
            hints[hintCount++] = "3:Next";
        }
    }
    
    // Use efficient row-by-row layout instead of bulk clearing
    drawRowByRowLayout(tft, dataState, hints, hintCount);
    
    // Draw content only if needed
    if (needsFullLayout || needsContentUpdate) {
        drawNodesContent(tft, dataState);
    }
}

void NodesListScreen::drawNodesContent(Adafruit_ST7789& tft, UIDataState& dataState) {
    const auto& nodesData = dataState.getNodesData();
    int startIndex = getPageStartRow();
    int rowsPerPage = getRowsPerPage();
    int rowIndex = 0;
    
    // Draw page info first if paginated
    if (shouldShowPageInfo()) {
        if (isRowDirty(rowIndex) || getNeedsFullRedraw()) {
            drawPageInfo(tft, nodesData.filteredNodeCount, rowIndex);
        }
        rowIndex++;
    }
    
    // Draw nodes for current page - only redraw dirty rows
    for (int i = 0; i < rowsPerPage && startIndex + i < (int)nodesData.filteredNodeCount && rowIndex < CONTENT_ROWS; i++, rowIndex++) {
        if (isRowDirty(rowIndex) || getNeedsFullRedraw()) {
            int nodeIndex = startIndex + i;
            drawNodeRow(tft, nodesData, nodeIndex, rowIndex);
        }
    }
    
    // Clear any remaining dirty rows that don't have content
    for (; rowIndex < CONTENT_ROWS; rowIndex++) {
        if (isRowDirty(rowIndex) || getNeedsFullRedraw()) {
            int y = CONTENT_Y + BORDER_WIDTH + (rowIndex * ROW_HEIGHT);
            clearRect(tft, CONTENT_X + 4, y, CONTENT_WIDTH - 8, ROW_HEIGHT);
        }
    }
    
    // Mark all rows as clean after drawing
    clearAllRowsDirty();
}

void NodesListScreen::drawNodeRow(Adafruit_ST7789& tft, const UIDataState::NodesData& nodesData, int nodeIndex, int rowIndex) {
    // Calculate Y position within content area, accounting for borders
    int y = CONTENT_Y + BORDER_WIDTH + (rowIndex * ROW_HEIGHT);
    
    // Determine colors based on node status
    uint32_t currentTime = millis();
    uint16_t textColor = COLOR_TEXT;
    uint16_t bgColor = COLOR_BACKGROUND;
    
    if (nodesData.lastHeard[nodeIndex] > 0) {
        uint32_t secondsAgo = (currentTime - nodesData.lastHeard[nodeIndex]) / 1000;
        if (secondsAgo <= 60) {
            textColor = ST77XX_BLACK;
            bgColor = ST77XX_GREEN;   // < 1 minute = green
        } else if (secondsAgo <= 900) {
            textColor = ST77XX_BLACK;
            bgColor = ST77XX_YELLOW;  // < 15 minutes = yellow
        } else if (secondsAgo <= 10800) {
            textColor = ST77XX_WHITE;
            bgColor = 0xFD20;  // Orange color (RGB565) - < 3 hours
        } else {
            textColor = COLOR_ERROR;  // > 3 hours = red text
        }
    }
    
    // Format node info with signal strength
    char nodeStr[64];
    const char* displayName = nodesData.nodeList[nodeIndex];
    int8_t rssi = nodesData.signalStrength[nodeIndex];
    
    // Show name and signal strength
    if (rssi != 0) {
        snprintf(nodeStr, sizeof(nodeStr), "%.12s %ddB", displayName, rssi);
    } else {
        snprintf(nodeStr, sizeof(nodeStr), "%.16s", displayName);
    }
    
    // Add time info if recent
    if (nodesData.lastHeard[nodeIndex] > 0) {
        uint32_t secondsAgo = (currentTime - nodesData.lastHeard[nodeIndex]) / 1000;
        if (secondsAgo < 3600) { // Only show time if less than 1 hour
            char timeStr[16];
            if (secondsAgo < 60) {
                snprintf(timeStr, sizeof(timeStr), " %us", (unsigned)secondsAgo);
            } else {
                snprintf(timeStr, sizeof(timeStr), " %um", (unsigned)(secondsAgo / 60));
            }
            strncat(nodeStr, timeStr, sizeof(nodeStr) - strlen(nodeStr) - 1);
        }
    }
    
    // Draw the row with background color if status indicates
    if (bgColor != COLOR_BACKGROUND) {
        // Draw colored background
        drawTextInRect(tft, CONTENT_X + 4, y, CONTENT_WIDTH - 8, ROW_HEIGHT,
                       nodeStr, textColor, bgColor);
    } else {
        // Draw normal text
        drawTextInRect(tft, CONTENT_X + 4, y, CONTENT_WIDTH - 8, ROW_HEIGHT,
                       nodeStr, textColor, COLOR_BACKGROUND);
    }
}

