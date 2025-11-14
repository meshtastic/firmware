#include "NodesListScreen.h"
#include "UINavigator.h"
#include <Arduino.h>

NodesListScreen::NodesListScreen(UINavigator* navigator) 
    : BaseScreen(navigator, "NODES"), selectedIndex(0), scrollOffset(0) {
}

void NodesListScreen::onEnter() {
    LOG_INFO("🔧 UI: Entering Nodes List Screen");
    markForFullRedraw();
    selectedIndex = 0;
    scrollOffset = 0;
}

void NodesListScreen::onExit() {
    LOG_INFO("🔧 UI: Exiting Nodes List Screen");
}

void NodesListScreen::handleInput(uint8_t input) {
    const auto& nodesData = navigator->getDataState().getNodesData();
    
    switch (input) {
        case 1: // User button - go back to home
            navigator->navigateBack();
            break;
        case 2: // Up navigation (if you add more buttons later)
            if (selectedIndex > 0) {
                selectedIndex--;
                adjustScrollOffset(nodesData.nodeCount);
                markForFullRedraw();
            }
            break;
        case 3: // Down navigation (if you add more buttons later)
            if (selectedIndex < (int)nodesData.nodeCount - 1) {
                selectedIndex++;
                adjustScrollOffset(nodesData.nodeCount);
                markForFullRedraw();
            }
            break;
        default:
            LOG_DEBUG("🔧 UI: Unhandled input: %d", input);
            break;
    }
}

bool NodesListScreen::needsUpdate(UIDataState& dataState) {
    return dataState.isNodesDataChanged();
}

void NodesListScreen::draw(Adafruit_ST7789& tft, UIDataState& dataState) {
    tft.fillScreen(ST77XX_BLACK);
    
    const auto& nodesData = dataState.getNodesData();
    
    // Draw main border (like Python GUI)
    tft.drawRect(0, 0, 320, 240, ST77XX_GREEN);
    
    // Header section
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_GREEN);
    tft.setCursor(8, 8);
    tft.print("MESH NODES");
    
    // Node count on right
    char countStr[16];
    snprintf(countStr, sizeof(countStr), "TOTAL: %u", (unsigned)nodesData.nodeCount);
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(countStr, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor(320 - w - 8, 8);
    tft.print(countStr);
    
    // Header separator line
    tft.drawLine(8, 25, 312, 25, ST77XX_GREEN);
    
    if (nodesData.nodeCount == 0) {
        // No nodes message (centered)
        tft.setTextColor(ST77XX_YELLOW);
        const char* msg = "[ NO NODES DISCOVERED ]";
        tft.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
        tft.setCursor((320 - w) / 2, 110);
        tft.print(msg);
        
        tft.setTextColor(ST77XX_CYAN);
        const char* hint = "Waiting for mesh traffic...";
        tft.getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
        tft.setCursor((320 - w) / 2, 130);
        tft.print(hint);
    } else {
        // Column headers
        tft.setTextColor(ST77XX_CYAN);
        tft.setCursor(8, 35);
        tft.print("ID       NAME         LAST HEARD");
        tft.drawLine(8, 45, 312, 45, ST77XX_CYAN);
        
        // Draw nodes in organized rows
        int yPos = 55;
        unsigned maxNodes = (nodesData.nodeCount < 16) ? nodesData.nodeCount : 16; // Max 16 nodes can be displayed
        
        for (unsigned i = 0; i < maxNodes; i++) {
            // Standard node display (no selection highlighting for now)
            tft.setTextColor(ST77XX_WHITE);
            
            // Format node info in columns - using actual data structure
            char nodeStr[32];
            snprintf(nodeStr, sizeof(nodeStr), "%08X", (unsigned int)nodesData.nodeIds[i]);
            tft.setCursor(8, yPos);
            tft.printf("%-8s", nodeStr);
            
            // Node name (truncated to fit)
            char shortName[12];
            if (strlen(nodesData.nodeList[i]) > 0) {
                strncpy(shortName, nodesData.nodeList[i], 11);
                shortName[11] = '\0';
            } else {
                strcpy(shortName, "Unknown");
            }
            tft.setCursor(80, yPos);
            tft.printf("%-11s", shortName);
            
            // Last heard time (simplified - just show seconds ago)
            tft.setCursor(190, yPos);
            uint32_t currentTime = millis();
            if (nodesData.lastHeard[i] > 0) {
                uint32_t secondsAgo = (currentTime - nodesData.lastHeard[i]) / 1000;
                if (secondsAgo < 60) {
                    tft.printf("%us", (unsigned)secondsAgo);
                } else if (secondsAgo < 3600) {
                    tft.printf("%um", (unsigned)(secondsAgo / 60));
                } else {
                    tft.printf("%uh", (unsigned)(secondsAgo / 3600));
                }
            } else {
                tft.print("---");
            }
            
            // Status indicator (green for recent, yellow for old, red for very old)
            uint16_t statusColor = ST77XX_GREEN; // Default to green
            if (nodesData.lastHeard[i] > 0) {
                uint32_t secondsAgo = (currentTime - nodesData.lastHeard[i]) / 1000;
                if (secondsAgo > 3600) statusColor = ST77XX_RED;      // > 1 hour
                else if (secondsAgo > 300) statusColor = ST77XX_YELLOW; // > 5 minutes
            }
            
            tft.fillRect(300, yPos + 2, 8, 8, statusColor);
            
            yPos += 15;
            if (yPos > 200) break; // Don't draw beyond screen
        }
        
        // Show count if limited
        if (nodesData.nodeCount > 16) {
            tft.setTextColor(ST77XX_YELLOW);
            tft.setCursor(8, 210);
            tft.printf("Showing first 16 of %u nodes", (unsigned)nodesData.nodeCount);
        }
    }
    
    // Footer with navigation hints
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(8, 220);
    tft.print("[BTN] Back to Home");
}

void NodesListScreen::drawNodeList(Adafruit_ST7789& tft, const UIDataState::NodesData& data, bool forceRedraw) {
    if (!forceRedraw) return; // For now, implement fine-grained updating later
    
    // Show node count
    tft.setTextColor(COLOR_ACCENT, COLOR_BACKGROUND);
    tft.setTextSize(1);
    tft.setCursor(5, CONTENT_START_Y);
    char countStr[32];
    snprintf(countStr, sizeof(countStr), "Total Nodes: %d", (int)data.nodeCount);
    tft.print(countStr);
    
    if (data.nodeCount == 0) {
        tft.setTextColor(COLOR_WARNING, COLOR_BACKGROUND);
        tft.setCursor(5, CONTENT_START_Y + 20);
        tft.print("No nodes discovered yet");
        return;
    }
    
    // Draw scroll indicator if needed
    if (data.nodeCount > NODES_PER_PAGE) {
        tft.setTextColor(COLOR_ACCENT, COLOR_BACKGROUND);
        tft.setCursor(SCREEN_WIDTH - 60, CONTENT_START_Y);
        char scrollStr[16];
        int totalPages = (data.nodeCount + NODES_PER_PAGE - 1) / NODES_PER_PAGE;
        int currentPage = (scrollOffset / NODES_PER_PAGE) + 1;
        snprintf(scrollStr, sizeof(scrollStr), "(%d/%d)", currentPage, totalPages);
        tft.print(scrollStr);
    }
    
    // Draw node list
    int y = CONTENT_START_Y + 20;
    int displayedNodes = 0;
    
    for (size_t i = scrollOffset; i < data.nodeCount && i < 16 && displayedNodes < NODES_PER_PAGE; i++) {
        bool isSelected = (i == selectedIndex);
        drawNodeEntry(tft, data.nodeList[i], data.nodeIds[i], data.lastHeard[i], i, y, isSelected);
        y += 20;
        displayedNodes++;
    }
    
    // Show navigation hints if there are multiple nodes
    if (data.nodeCount > 1) {
        tft.setTextColor(COLOR_ACCENT, COLOR_BACKGROUND);
        tft.setCursor(5, SCREEN_HEIGHT - 40);
        tft.print("Use additional buttons for navigation");
    }
}

void NodesListScreen::drawNodeEntry(Adafruit_ST7789& tft, const char* nodeName, uint32_t nodeId, 
                                   uint32_t lastHeard, int index, int y, bool selected) {
    
    // Background for selected item
    if (selected) {
        tft.fillRect(0, y - 2, SCREEN_WIDTH, 16, COLOR_ACCENT);
        tft.setTextColor(COLOR_BACKGROUND, COLOR_ACCENT);
    } else {
        tft.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    }
    
    tft.setTextSize(1);
    
    // Node number/index
    char indexStr[8];
    snprintf(indexStr, sizeof(indexStr), "%d.", index + 1);
    tft.setCursor(5, y);
    tft.print(indexStr);
    
    // Node name or ID
    tft.setCursor(25, y);
    if (strlen(nodeName) > 0) {
        // Show name, truncate if too long
        char displayName[20];
        strncpy(displayName, nodeName, sizeof(displayName) - 1);
        displayName[sizeof(displayName) - 1] = '\0';
        tft.print(displayName);
    } else {
        // Show node ID
        char nodeIdStr[16];
        snprintf(nodeIdStr, sizeof(nodeIdStr), "%08X", nodeId);
        tft.print(nodeIdStr);
    }
    
    // Node status
    tft.setCursor(SCREEN_WIDTH - 80, y);
    uint16_t statusColor;
    const char* statusText = getNodeStatusText(lastHeard, statusColor);
    
    if (!selected) {
        tft.setTextColor(statusColor, COLOR_BACKGROUND);
    }
    tft.print(statusText);
}

const char* NodesListScreen::getNodeStatusText(uint32_t lastHeard, uint16_t& color) {
    uint32_t currentTime = millis() / 1000;
    
    if (lastHeard == 0) {
        color = COLOR_WARNING;
        return "UNKNOWN";
    }
    
    uint32_t timeSince = currentTime > lastHeard ? currentTime - lastHeard : 0;
    
    if (timeSince < 300) { // 5 minutes
        color = COLOR_SUCCESS;
        return "ONLINE";
    } else if (timeSince < 3600) { // 1 hour
        color = COLOR_WARNING;
        return "RECENT";
    } else {
        color = COLOR_ERROR;
        return "OFFLINE";
    }
}

void NodesListScreen::adjustScrollOffset(size_t nodeCount) {
    // Ensure selected item is visible
    if (selectedIndex < scrollOffset) {
        scrollOffset = selectedIndex;
    } else if (selectedIndex >= scrollOffset + NODES_PER_PAGE) {
        scrollOffset = selectedIndex - NODES_PER_PAGE + 1;
    }
    
    // Ensure scroll offset is within bounds
    if (scrollOffset < 0) {
        scrollOffset = 0;
    }
    
    int maxScrollOffset = (int)nodeCount - NODES_PER_PAGE;
    if (maxScrollOffset < 0) {
        maxScrollOffset = 0;
    }
    
    if (scrollOffset > maxScrollOffset) {
        scrollOffset = maxScrollOffset;
    }
}