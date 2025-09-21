/*
BaseUI

Developed and Maintained By:
- Ronald Garcia (HarukiToreda) – Lead development and implementation.
- JasonP (Xaositek)  – Screen layout and icon design, UI improvements and testing.
- TonyG (Tropho) – Project management, structural planning, and testing

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/
#include "Screen.h"
#include "NodeDB.h"
#include "PowerMon.h"
#include "Throttle.h"
#include "configuration.h"
#if HAS_SCREEN
#include <OLEDDisplay.h>

#include "DisplayFormatters.h"
#include "TimeFormatters.h"
#include "draw/ClockRenderer.h"
#include "draw/DebugRenderer.h"
#include "draw/MenuHandler.h"
#include "draw/MessageRenderer.h"
#include "draw/NodeListRenderer.h"
#include "draw/NotificationRenderer.h"
#include "draw/UIRenderer.h"
#include "modules/CannedMessageModule.h"
#include "modules/ChatHistoryStore.h"

#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#include "buzz.h"
#endif
#include "FSCommon.h"
#include "MeshService.h"
#include "RadioLibInterface.h"
#include "error.h"
#include "gps/GeoCoord.h"
#include "gps/RTC.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/emotes.h"
#include "graphics/images.h"
#include "input/TouchScreenImpl1.h"
#include "main.h"
#include "mesh-pb-constants.h"
#include "mesh/Channels.h"
#include "mesh/generated/meshtastic/deviceonly.pb.h"
#include "meshUtils.h"
#include "modules/ExternalNotificationModule.h"
#include "modules/TextMessageModule.h"
#include "modules/WaypointModule.h"
#include "sleep.h"
#include "target_specific.h"
#include "mesh/MeshTypes.h"   // for NODENUM_BROADCAST
#include "input/RotaryEncoderInterruptImpl1.h"

#include <set>
#include <map>
#include <vector>
#include <algorithm>

using graphics::Emote;
using graphics::emotes;
using graphics::numEmotes;

extern uint16_t TFT_MESH;
extern bool g_chatScrollByPress;  // comes from MenuHandler.cpp
extern bool g_chatScrollUpDown;   // comes from MenuHandler.cpp
extern RotaryEncoderInterruptImpl1 *rotaryEncoderInterruptImpl1;
extern graphics::Screen *screen;  // Global screen instance

// Global variable for chat silent mode
bool g_chatSilentMode = false;

#if HAS_WIFI && !defined(ARCH_PORTDUINO)
#include "mesh/wifi/WiFiAPClient.h"
#endif

#ifdef ARCH_ESP32
#endif

#if ARCH_PORTDUINO
#include "modules/StoreForwardModule.h"
#include "platform/portduino/PortduinoGlue.h"
#endif

using namespace meshtastic; /** @todo remove */

// ScrollState definition for chat scrolling
struct ScrollState {
    int sel = 0;            // selected line (0..visible-1)
    int scrollIndex = 0;    // first visible message (sliding window)
    int offset = 0;         // horizontal offset (characters)
    uint32_t lastMs = 0;    // last update
};

// Global variables for chat functionality
std::string g_pendingKeyboardHeader;
std::set<uint8_t> g_favChannelTabs;
std::map<uint32_t, ScrollState> g_nodeScroll; //  node (DM)
std::map<uint8_t , ScrollState> g_chanScroll; //  channel

namespace graphics
{

// Alias for global ScrollState to avoid conflicts
using GlobalScrollState = ::ScrollState;

// This means the *visible* area (sh1106 can address 132, but shows 128 for example)
#define IDLE_FRAMERATE 1 // in fps




// --- return to the same chat after sending text ---
static int  s_returnToFrame   = -1;
static bool s_reFocusAfterSend = false;


// Seed to open channel chat tabs at startup
static bool s_seededChannelTabs = false;
static void seedChannelTabsFromConfig();

// --- Helpers to filter options according to CardKB ---
static uint8_t filterByCardKB(const char* const *srcOptions, const int *srcEnums, uint8_t srcCount,
                              const char** dstOptions, int* dstEnums)
{
    // Rule:
    //  - With CardKB (kb_found=true): hide "New preset msg"
    //  - Without CardKB (kb_found=false): hide "New text msg"
    uint8_t n = 0;
    for (uint8_t i = 0; i < srcCount; ++i) {
        const char* s = srcOptions[i];
        if (!s) continue;

        bool isPreset = (strncmp(s, "New preset msg", 14) == 0);
        bool isText   = (strncmp(s, "New text msg", 12)   == 0);

        if (kb_found) {
            if (isPreset) continue; // hide "New preset msg"
        } else {
            if (isText) continue;   // hide "New text msg"
        }

        dstOptions[n] = s;
        if (srcEnums) dstEnums[n] = srcEnums[i];
        ++n;
    }
    return n;
}

static void showMenuFilteredByCardKB(const char* title,
                                     const char* const *options, const int *enums, uint8_t count,
                                     std::function<void(int)> cb)
{
    // Static buffers to ensure lifetime until the user selects
    static const char* filteredOpts[20];
    static int         filteredEnums[20];

    uint8_t filteredCount = filterByCardKB(options, enums, count, filteredOpts, filteredEnums);

    // If there are no options left, exit without showing anything
    if (filteredCount == 0) {
        return;
    }

    // Selection banner assembly
    NotificationRenderer::resetBanner();
    strlcpy(NotificationRenderer::alertBannerMessage, title, sizeof(NotificationRenderer::alertBannerMessage));
    NotificationRenderer::curSelected           = 0;
    NotificationRenderer::alertBannerOptions    = filteredCount;
    NotificationRenderer::optionsArrayPtr       = filteredOpts;
    NotificationRenderer::optionsEnumPtr        = (enums ? filteredEnums : nullptr);
    NotificationRenderer::alertBannerCallback   = cb;
    NotificationRenderer::alertBannerUntil      = 0;
    NotificationRenderer::current_notification_type = notificationTypeEnum::selection_picker;
}

// === Helpers to show age of last message with s/m/h/D ===
static String ageLabel(uint32_t tsSec)
{
    uint32_t nowSec = (uint32_t)time(nullptr);
    if (nowSec == 0) {
        nowSec = millis() / 1000;
    }

    uint32_t diff   = (nowSec > tsSec) ? (nowSec - tsSec) : 0;

    if (diff < 60) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%lus", (unsigned long)diff);
        return String(buf);
    } else if (diff < 3600) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%lum", (unsigned long)(diff / 60));
        return String(buf);
    } else if (diff < 86400) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%luh", (unsigned long)(diff / 3600));
        return String(buf);
    } else {
        char buf[8];
        snprintf(buf, sizeof(buf), "%luD", (unsigned long)(diff / 86400));
        return String(buf);
    }
}

static String currentChatAgeLabel(uint32_t nodeIdOrDest, uint8_t ch)
{
    uint32_t ts = 0;
    bool ok = false;

    if (nodeIdOrDest == NODENUM_BROADCAST) {
        const auto& v = chat::ChatHistoryStore::instance().getCHAN(ch);
        if (!v.empty()) { ts = v.back().ts; ok = true; }
    } else {
        const auto& v = chat::ChatHistoryStore::instance().getDM(nodeIdOrDest);
        if (!v.empty()) { ts = v.back().ts; ok = true; }
    }

    return ok ? ageLabel(ts) : String("");
}


// === Chat tabs: state & draw helpers ===
static std::vector<uint32_t> g_favChatNodes;
static size_t g_favChatFirst = (size_t)-1;
static size_t g_favChatLast  = (size_t)-1;

static std::vector<uint8_t>  g_chanTabs;
static size_t g_chanTabFirst = (size_t)-1;
static size_t g_chanTabLast  = (size_t)-1;

// Channel "favorites" managed only from Screen.cpp - moved outside namespace

static void seedChannelTabsFromConfig()
{
    if (s_seededChannelTabs) return;
    s_seededChannelTabs = true;
    int n = channels.getNumChannels();
    for (int i = 0; i < n; ++i) {
        const meshtastic_Channel c = channels.getByIndex(i);
        bool present = (i == 0);
        if (c.settings.name[0]) present = true;
        if (present) g_favChannelTabs.insert((uint8_t)i);
    }
}


// ===== Horizontal scroll only on selected line =====
static bool g_chatScrollActive = false; // true if any frame drew marquee this cycle

// ==== ScrollState for DM/Channel tracking ====
struct ScrollState {
    int sel = 0;            // selected line (0..visible-1)
    int scrollIndex = 0;    // first visible message (sliding window)
    int offset = 0;         // horizontal offset (characters)
    uint32_t lastMs = 0;    // last update
};


// Marquee auto-scroll control
static uint32_t g_lastInteractionMs = 0;   // Last user interaction timestamp
static const uint32_t MARQUEE_TIMEOUT_MS = 30000; // 30 seconds timeout for marquee reset
static const uint32_t HOME_TIMEOUT_MS = 50000; // 40 seconds timeout for home return
static uint8_t g_previousFrame = 0xFF;     // Track frame changes for auto-scroll on enter

// Forward declarations
static void updateLastInteraction();

// Helpers (in case we ever treat channel as a "virtual node")
static inline bool isVirtualChannelNode(uint32_t nodeId) { return (nodeId & 0xC0000000u) == 0xC0000000u; }
static inline uint8_t channelOfVirtual(uint32_t nodeId)  { return (uint8_t)(nodeId & 0xFFu); }
static inline uint32_t makeVirtualChannelNode(uint8_t ch) { return 0xC0000000u | ch; }

// Marquee helper: returns window of 'cap' chars, advancing every ~200ms
static std::string marqueeSlice(const std::string& in, GlobalScrollState& st, int cap, bool advance)
{
    if ((int)in.size() <= cap) { st.offset = 0; return in; }

    const uint32_t stepMs = 200;
    const std::string sep = "   ";
    if (advance) {
        uint32_t now = millis();
        if (now - st.lastMs >= stepMs) {
            st.lastMs = now;
            st.offset = st.offset + 1;
        }
    }

    std::string padded = in + sep;
    int n = (int)padded.size();
    int o = (n > 0) ? (st.offset % n) : 0;

    if (o + cap <= n) return padded.substr(o, cap);
    std::string s1 = padded.substr(o);
    return s1 + padded.substr(0, cap - (int)s1.size());
}

// Marquee auto-scroll functions
static void updateLastInteraction() {
    g_lastInteractionMs = millis();
}

void resetScrollToTop(uint32_t nodeId, bool isDM) {
    if (!screen) return;  // Use global screen instance

    if (isDM) {
        GlobalScrollState &st = g_nodeScroll[nodeId];
        const auto& dmHistory = chat::ChatHistoryStore::instance().getDM(nodeId);
        int totalMessages = (int)dmHistory.size();
        if (totalMessages > 0) {
            // Find the last read message to position there
            int lastReadIdx = chat::ChatHistoryStore::instance().getLastReadIndexDM(nodeId);

            if (lastReadIdx >= 0) {
                // Position the last read message on the first line (row 0)
                // itemIndex = total - 1 - (scrollIndex + row), we want lastReadIdx on row 0
                // so: lastReadIdx = total - 1 - (scrollIndex + 0) => scrollIndex = total - 1 - lastReadIdx
                st.scrollIndex = totalMessages - 1 - lastReadIdx;
                st.sel = 0;  // Marquee on the first line (last read)
            } else {
                // If no messages are read, go to the newest (first line)
                st.scrollIndex = 0;
                st.sel = 0;
            }
            st.offset = 0;       // Reset horizontal scroll too
            st.lastMs = millis();
        }
    } else {
        uint8_t ch = (uint8_t)nodeId;
        GlobalScrollState &st = g_chanScroll[ch];
        const auto& chanHistory = chat::ChatHistoryStore::instance().getCHAN(ch);
        int totalMessages = (int)chanHistory.size();
        if (totalMessages > 0) {
            // Find the last read message to position there
            int lastReadIdx = chat::ChatHistoryStore::instance().getLastReadIndexCHAN(ch);

            if (lastReadIdx >= 0) {
                // Position the last read message on the first line (row 0)
                // itemIndex = total - 1 - (scrollIndex + row), we want lastReadIdx on row 0
                // so: lastReadIdx = total - 1 - (scrollIndex + 0) => scrollIndex = total - 1 - lastReadIdx
                st.scrollIndex = totalMessages - 1 - lastReadIdx;
                st.sel = 0;  // Marquee on the first line (last read)
            } else {
                // If no messages are read, go to the newest (first line)
                st.scrollIndex = 0;
                st.sel = 0;
            }
            st.offset = 0;       // Reset horizontal scroll too
            st.lastMs = millis();
        }
    }
}

void Screen::checkInactivityTimeouts() {
    if (g_lastInteractionMs == 0) {
        g_lastInteractionMs = millis(); // Initialize on first call
        return;
    }

    uint32_t now = millis();
    uint32_t inactiveTime = now - g_lastInteractionMs;

    // 30 seconds without interaction - reset marquee/scroll position
    if (inactiveTime >= MARQUEE_TIMEOUT_MS) {
        if (getUI() && isShowingNormalScreen()) {
            uint8_t currentFrame = getUI()->getUiState()->currentFrame;

            // Reset scroll positions for current chat if in a chat frame
            // Check if we're in a DM chat
            if (g_favChatFirst != (size_t)-1 && currentFrame >= g_favChatFirst && currentFrame <= g_favChatLast) {
                size_t index = currentFrame - g_favChatFirst;
                if (index < g_favChatNodes.size()) {
                    uint32_t nodeId = g_favChatNodes[index];
                    resetScrollToTop(nodeId, true);
                    LOG_DEBUG("Marquee timeout: reset DM scroll for node %08x", nodeId);
                }
            }
            // Check if we're in a channel chat
            else if (g_chanTabFirst != (size_t)-1 && currentFrame >= g_chanTabFirst && currentFrame <= g_chanTabLast) {
                size_t index = currentFrame - g_chanTabFirst;
                if (index < g_chanTabs.size()) {
                    uint8_t ch = g_chanTabs[index];
                    resetScrollToTop(ch, false);
                    LOG_DEBUG("Marquee timeout: reset channel scroll for ch %d", ch);
                }
            }
        }
    }

    // 0 seconds without interaction - return to home frame and reset scroll
    if (inactiveTime >= HOME_TIMEOUT_MS) {
        if (getUI() && isShowingNormalScreen()) {
            uint8_t currentFrame = getUI()->getUiState()->currentFrame;

            // If not on home frame (frame 0), go to home
            if (currentFrame != 0) {
                LOG_DEBUG("Home timeout: returning to home frame from frame %d", currentFrame);
                getUI()->switchToFrame(0);
                forceDisplay();
            }

            // Reset scroll positions for current chat if in a chat frame
            // Check if we're in a DM chat
            if (g_favChatFirst != (size_t)-1 && currentFrame >= g_favChatFirst && currentFrame <= g_favChatLast) {
                size_t index = currentFrame - g_favChatFirst;
                if (index < g_favChatNodes.size()) {
                    uint32_t nodeId = g_favChatNodes[index];
                    resetScrollToTop(nodeId, true);
                    LOG_DEBUG("Home timeout: reset DM scroll for node %08x", nodeId);
                }
            }
            // Check if we're in a channel chat
            else if (g_chanTabFirst != (size_t)-1 && currentFrame >= g_chanTabFirst && currentFrame <= g_chanTabLast) {
                size_t index = currentFrame - g_chanTabFirst;
                if (index < g_chanTabs.size()) {
                    uint8_t ch = g_chanTabs[index];
                    resetScrollToTop(ch, false);
                    LOG_DEBUG("Home timeout: reset channel scroll for ch %d", ch);
                }
            }
        }
        g_lastInteractionMs = now; // Reset timer only after going home
    }
}

void checkFrameChange() {
    if (!screen || !screen->getUI() || !screen->isShowingNormalScreen()) return;

    uint8_t currentFrame = screen->getUI()->getUiState()->currentFrame;

    // Check if frame has changed
    if (g_previousFrame != 0xFF && g_previousFrame != currentFrame) {
        // Frame changed - check if we entered a chat frame
        bool enteredChat = false;

        // Check if we entered a DM chat
        if (g_favChatFirst != (size_t)-1 && currentFrame >= g_favChatFirst && currentFrame <= g_favChatLast) {
            size_t index = currentFrame - g_favChatFirst;
            if (index < g_favChatNodes.size()) {
                uint32_t nodeId = g_favChatNodes[index];
                resetScrollToTop(nodeId, true);
                LOG_DEBUG("Frame change: reset DM scroll for node %08x (frame %d->%d)", nodeId, g_previousFrame, currentFrame);
                enteredChat = true;
            }
        }
        // Check if we entered a channel chat
        else if (g_chanTabFirst != (size_t)-1 && currentFrame >= g_chanTabFirst && currentFrame <= g_chanTabLast) {
            size_t index = currentFrame - g_chanTabFirst;
            if (index < g_chanTabs.size()) {
                uint8_t ch = g_chanTabs[index];
                resetScrollToTop(ch, false);
                LOG_DEBUG("Frame change: reset channel scroll for ch %d (frame %d->%d)", ch, g_previousFrame, currentFrame);
                enteredChat = true;
            }
        }

        if (enteredChat) {
            updateLastInteraction(); // Reset timeout when entering chat
        }
    }

    g_previousFrame = currentFrame;
}


// Small text line helper
static void drawLineSmall(OLEDDisplay *display, int16_t x, int16_t y, const char* s) {
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    display->drawString(x, y, s);
}

// Function to detect if a message needs extra height (emotes or line breaks)
static bool needsExtraHeight(const std::string &text) {
    // Check for multiple newlines (count them)
    size_t newlineCount = 0;
    size_t pos = 0;
    while ((pos = text.find('\n', pos)) != std::string::npos) {
        newlineCount++;
        pos++;
    }
    if (newlineCount > 0) {
        return true;
    }

    // Check for emotes
    for (int i = 0; i < numEmotes; ++i) {
        if (text.find(emotes[i].label) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// Function to draw with large emotes when needed, preserving marquee for name part
static void drawLineWithEmotes(OLEDDisplay *display, int16_t x, int16_t y, const char* s) {
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    
    std::string text(s);
    graphics::MessageRenderer::drawStringWithEmotes(display, x, y, text, emotes, numEmotes);
}



static void drawFavNodeChatFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    if (g_favChatFirst == (size_t)-1 || g_favChatLast == (size_t)-1) return;
    uint8_t cf = state->currentFrame;
    size_t idx = (size_t)cf - g_favChatFirst;
    if (idx >= g_favChatNodes.size()) return;

    uint32_t nodeId = g_favChatNodes[idx];
    using chat::ChatHistoryStore;
    auto &store = ChatHistoryStore::instance();
    const auto &q = store.getDM(nodeId);

    const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeId);
    const char* alias = (node && node->has_user && node->user.long_name[0]) ? node->user.long_name : nullptr;

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    // === Dynamic time according to selected message ===
    GlobalScrollState &st = g_nodeScroll[nodeId];
    uint32_t tsSel = 0;
    if (!q.empty()) {
        int i = (int)q.size() - 1 - st.sel;
        if (i >= 0 && i < (int)q.size()) {
            tsSel = q[i].ts;
        }
    }
    String age = (tsSel > 0) ? ageLabel(tsSel) : String("");

    // Get unread message count for this specific DM
    int unreadCount = store.getUnreadCountDM(nodeId);

    char title[64];
    if (unreadCount > 0) {
        // Show unread count alongside the title
        if (alias)  std::snprintf(title, sizeof(title), "%s (%s) (%d)", alias, age.c_str(), unreadCount);
        else        std::snprintf(title, sizeof(title), "%08X (%s) (%d)", (unsigned)nodeId, age.c_str(), unreadCount);
    } else {
        // No unread messages, show normal title
        if (alias)  std::snprintf(title, sizeof(title), "%s (%s)", alias, age.c_str());
        else        std::snprintf(title, sizeof(title), "%08X (%s)", (unsigned)nodeId, age.c_str());
    }
    display->drawString(x, y, title);

    const int lineH = 10;
    const int top   = y + 16;
    const int h     = display->height();
    const int maxLines = 4; // Fixed number of logical lines

    display->setFont(FONT_SMALL);

    const int total = (int)q.size();
    const int visibleRows = std::min(total, maxLines);
    if (visibleRows <= 0) {
        drawLineSmall(display, x, top, "Waiting...");
        return;
    }

    // Clamp scrollIndex with better logic for variable height messages
    if (st.scrollIndex > total - 1) st.scrollIndex = total - 1;
    if (st.scrollIndex < 0) st.scrollIndex = 0;
    // Clamp sel
    if (st.sel < 0) st.sel = 0;
    if (st.sel >= visibleRows) st.sel = visibleRows - 1;

    for (int row = 0; row < visibleRows; ++row) {
        int itemIndex = total - 1 - (st.scrollIndex + row);
        if (itemIndex < 0) break;
        const auto &e = q[itemIndex];
        std::string who = e.outgoing ? "S" : "R";

        // Add asterisk for unread messages (only when marquee is not active)
        std::string unreadIndicator = "";
        if (e.unread && !e.outgoing && row != st.sel) {
            unreadIndicator = "*";
        }
        
        // Mark message as read when selected (marquee active)
        if (row == st.sel && e.unread && !e.outgoing) {
            chat::ChatHistoryStore::instance().markMessageAsRead(nodeId, itemIndex);
        }
        
        std::string base = unreadIndicator + who + ": " + e.text;
        
        // Check if this message needs extra height
        bool needsExtra = needsExtraHeight(base);
        int currentLineH = needsExtra ? lineH * 3 : lineH; // Triple height for emotes/newlines
        
        std::string view;
        bool needScroll = false;
        const int cap = 22;
        if (row == st.sel) {
            view = marqueeSlice(base, st, cap, /*advance*/ true);
            needScroll = ((int)base.size() > cap);
        } else {
            if ((int)base.size() > cap) view = base.substr(0, cap - 3) + "...";
            else view = base;
        }
        if (needScroll) g_chatScrollActive = true;
        
        // Calculate Y position with dynamic spacing
        int lineY = top;
        for (int r = 0; r < row; ++r) {
            int prevIndex = total - 1 - (st.scrollIndex + r);
            if (prevIndex >= 0) {
                const auto &prevE = q[prevIndex];
                std::string prevWho = prevE.outgoing ? "S" : "R";
                std::string prevBase = prevWho + ": " + prevE.text;
                bool prevNeedsExtra = needsExtraHeight(prevBase);
                lineY += prevNeedsExtra ? lineH * 3 : lineH;
            }
        }
        
        // Render message - be more permissive with clipping to avoid skipping messages
        if (row == st.sel) {
            display->fillRect(x, lineY, display->getWidth(), currentLineH);
            display->setColor(BLACK);
            if (needsExtra) {
                drawLineWithEmotes(display, x, lineY, view.c_str());
            } else {
                drawLineSmall(display, x, lineY, view.c_str());
            }
            display->setColor(WHITE);
        } else {
            if (needsExtra) {
                drawLineWithEmotes(display, x, lineY, view.c_str());
            } else {
                drawLineSmall(display, x, lineY, view.c_str());
            }
        }
    }
}

static void drawChannelChatTabFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    if (g_chanTabFirst == (size_t)-1 || g_chanTabLast == (size_t)-1) return;
    uint8_t cf = state->currentFrame;
    size_t idx = (size_t)cf - g_chanTabFirst;
    if (idx >= g_chanTabs.size()) return;

    uint8_t ch = g_chanTabs[idx];
    using chat::ChatHistoryStore;
    auto &store = ChatHistoryStore::instance();
    const auto &q = store.getCHAN(ch);

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    const meshtastic_Channel c = channels.getByIndex(ch);
    const char *cname = (c.settings.name[0]) ? c.settings.name : nullptr;

    // === Dynamic time according to selected message ===
    GlobalScrollState &st = g_chanScroll[ch];
    uint32_t tsSel = 0;
    if (!q.empty()) {
        int i = (int)q.size() - 1 - st.sel;
        if (i >= 0 && i < (int)q.size()) {
            tsSel = q[i].ts;
        }
    }
    String age = (tsSel > 0) ? ageLabel(tsSel) : String("");

    // Get unread message count for this specific channel
    int unreadCount = store.getUnreadCountCHAN(ch);

    char title[64];
    if (unreadCount > 0) {
        // Show unread count alongside the title
        if (cname) std::snprintf(title, sizeof(title), "@%s (%s) (%d)", cname, age.c_str(), unreadCount);
        else       std::snprintf(title, sizeof(title), "@Channel %u (%s) (%d)", (unsigned)ch, age.c_str(), unreadCount);
    } else {
        // No unread messages, show normal title
        if (cname) std::snprintf(title, sizeof(title), "@%s (%s)", cname, age.c_str());
        else       std::snprintf(title, sizeof(title), "@Channel %u (%s)", (unsigned)ch, age.c_str());
    }
    display->drawString(x, y, title);

    const int lineH = 10;
    const int top   = y + 16;
    const int h     = display->height();
    const int maxLines = 4; // Fixed number of logical lines

    display->setFont(FONT_SMALL);

    const int total = (int)q.size();
    const int visibleRows = std::min(total, maxLines);
    if (visibleRows <= 0) {
        drawLineSmall(display, x, top, "Waiting...");
        return;
    }

    // Clamp scrollIndex
    if (st.scrollIndex > total - visibleRows) st.scrollIndex = total - visibleRows;
    if (st.scrollIndex < 0) st.scrollIndex = 0;
    // Clamp sel
    if (st.sel < 0) st.sel = 0;
    if (st.sel >= visibleRows) st.sel = visibleRows - 1;

    for (int row = 0; row < visibleRows; ++row) {
        int itemIndex = total - 1 - (st.scrollIndex + row);
        if (itemIndex < 0) break;
        const auto &e = q[itemIndex];
        std::string who;
        if (e.outgoing) who = "Send";
        else {
            const meshtastic_NodeInfoLite *sender = (e.node) ? nodeDB->getMeshNode(e.node) : nullptr;
            if (sender && sender->has_user && sender->user.long_name[0]) who = sender->user.long_name;
            else if (e.node) { char buf[9]; std::snprintf(buf, sizeof(buf), "%08X", (unsigned)e.node); who = buf; }
            else who = "??";
        }
        
        // Add asterisk for unread messages (only when marquee is not active)
        std::string unreadIndicator = "";
        if (e.unread && !e.outgoing && row != st.sel) {
            unreadIndicator = "*";
        }
        
        // Mark message as read when selected (marquee active)
        if (row == st.sel && e.unread && !e.outgoing) {
            chat::ChatHistoryStore::instance().markChannelMessageAsRead(ch, itemIndex);
        }
        
        std::string base = unreadIndicator + who + ": " + e.text;
        
        // Check if this message needs extra height
        bool needsExtra = needsExtraHeight(base);
        int currentLineH = needsExtra ? lineH * 3 : lineH; // Triple height for emotes/newlines
        
        std::string view;
        bool needScroll = false;
        const int cap = 28;
        if (row == st.sel) {
            view = marqueeSlice(base, st, cap, /*advance*/ true);
            needScroll = ((int)base.size() > cap);
        } else {
            if ((int)base.size() > cap) view = base.substr(0, cap - 3) + "...";
            else view = base;
        }
        if (needScroll) g_chatScrollActive = true;
        
        // Calculate Y position with dynamic spacing
        int lineY = top;
        for (int r = 0; r < row; ++r) {
            int prevIndex = total - 1 - (st.scrollIndex + r);
            if (prevIndex >= 0) {
                const auto &prevE = q[prevIndex];
                std::string prevWho;
                if (prevE.outgoing) prevWho = "Send";
                else {
                    const meshtastic_NodeInfoLite *prevSender = (prevE.node) ? nodeDB->getMeshNode(prevE.node) : nullptr;
                    if (prevSender && prevSender->has_user && prevSender->user.long_name[0]) prevWho = prevSender->user.long_name;
                    else if (prevE.node) { char buf[9]; std::snprintf(buf, sizeof(buf), "%08X", (unsigned)prevE.node); prevWho = buf; }
                    else prevWho = "??";
                }
                std::string prevBase = prevWho + ": " + prevE.text;
                bool prevNeedsExtra = needsExtraHeight(prevBase);
                lineY += prevNeedsExtra ? lineH * 3 : lineH;
            }
        }

        // Render message - be more permissive with clipping to avoid skipping messages
        if (row == st.sel) {
            display->fillRect(x, lineY, display->getWidth(), currentLineH);
            display->setColor(BLACK);
            if (needsExtra) {
                drawLineWithEmotes(display, x, lineY, view.c_str());
            } else {
                drawLineSmall(display, x, lineY, view.c_str());
            }
            display->setColor(WHITE);
        } else {
            if (needsExtra) {
                drawLineWithEmotes(display, x, lineY, view.c_str());
            } else {
                drawLineSmall(display, x, lineY, view.c_str());
            }
        }
    }
}

// Visible area
#define IDLE_FRAMERATE 1 // fps

// DEBUG
#define NUM_EXTRA_FRAMES 3 // text message and debug frame
// if defined a pixel will blink to show redraws
// #define SHOW_REDRAWS

// A text message frame + debug frame + all the node infos
FrameCallback *normalFrames;
static uint32_t targetFramerate = IDLE_FRAMERATE;
// Global variables for alert banner - explicitly define with extern "C" linkage to prevent optimization

uint32_t logo_timeout = 5000; // 4 seconds for EACH logo

// Threshold values for the GPS lock accuracy bar display
uint32_t dopThresholds[5] = {2000, 1000, 500, 200, 100};

// At some point, we're going to ask all of the modules if they would like to display a screen frame
// we'll need to hold onto pointers for the modules that can draw a frame.
std::vector<MeshModule *> moduleFrames;

// Global variables for screen function overlay symbols
std::vector<std::string> functionSymbol;
std::string functionSymbolString;

#if HAS_GPS
// GeoCoord object for the screen
GeoCoord geoCoord;
#endif

#ifdef SHOW_REDRAWS
static bool heartbeat = false;
#endif

#include "graphics/ScreenFonts.h"
#include <Throttle.h>

// Usage: int stringWidth = formatDateTime(datetimeStr, sizeof(datetimeStr), rtc_sec, display);
// End Functions to write date/time to the screen

extern bool hasUnreadMessage;

// ==============================
// Overlay Alert Banner Renderer
// ==============================
// Displays a temporary centered banner message (e.g., warning, status, etc.)
// The banner appears in the center of the screen and disappears after the specified duration

void Screen::openNodeInfoFor(NodeNum nodeNum)
{
    // Save which node should be shown
    graphics::UIRenderer::currentFavoriteNodeNum = nodeNum;

    // Create a FrameCallback with the drawNodeInfoDirect function
    setFrameImmediateDraw(new FrameCallback(
        [](OLEDDisplay *d, OLEDDisplayUiState *s, int16_t x, int16_t y) {
            graphics::UIRenderer::drawNodeInfoDirect(d, s, x, y);
        }
    ));
}

#if HAS_WIFI && !defined(ARCH_PORTDUINO)
void Screen::openMqttInfoScreen()
{
    // Set flag to track MQTT status screen is showing
    graphics::UIRenderer::showingMqttStatus = true;
    
    // Create a FrameCallback with the drawMqttInfoDirect function
    setFrameImmediateDraw(new FrameCallback(
        [](OLEDDisplay *d, OLEDDisplayUiState *s, int16_t x, int16_t y) {
            graphics::UIRenderer::drawMqttInfoDirect(d, s, x, y);
        }
    ));
}
#endif

void Screen::showSimpleBanner(const char *message, uint32_t durationMs)
{
    BannerOverlayOptions options;
    options.message = message;
    options.durationMs = durationMs;
    options.notificationType = notificationTypeEnum::text_banner;
    showOverlayBanner(options);
}

// Called to trigger a banner with custom message and duration
void Screen::showOverlayBanner(BannerOverlayOptions banner_overlay_options)
{
#ifdef USE_EINK
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // Skip full refresh for all overlay menus
#endif
    // Store the message and set the expiration timestamp
    strncpy(NotificationRenderer::alertBannerMessage, banner_overlay_options.message, 255);
    NotificationRenderer::alertBannerMessage[255] = '\0'; // Ensure null termination
    NotificationRenderer::alertBannerUntil =
        (banner_overlay_options.durationMs == 0) ? 0 : millis() + banner_overlay_options.durationMs;
    NotificationRenderer::optionsArrayPtr = banner_overlay_options.optionsArrayPtr;
    NotificationRenderer::optionsEnumPtr = banner_overlay_options.optionsEnumPtr;
    NotificationRenderer::alertBannerOptions = banner_overlay_options.optionsCount;
    NotificationRenderer::alertBannerCallback = banner_overlay_options.bannerCallback;
    NotificationRenderer::curSelected = banner_overlay_options.InitialSelected;
    NotificationRenderer::pauseBanner = false;
    NotificationRenderer::current_notification_type = notificationTypeEnum::selection_picker;
    static OverlayCallback overlays[] = {graphics::UIRenderer::drawNavigationBar, NotificationRenderer::drawBannercallback};
    ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));
    ui->setTargetFPS(60);
    ui->update();
}

// Called to trigger a banner with custom message and duration
void Screen::showNodePicker(const char *message, uint32_t durationMs, std::function<void(uint32_t)> bannerCallback)
{
#ifdef USE_EINK
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // Skip full refresh for all overlay menus
#endif
    nodeDB->pause_sort(true);
    // Store the message and set the expiration timestamp
    strncpy(NotificationRenderer::alertBannerMessage, message, 255);
    NotificationRenderer::alertBannerMessage[255] = '\0'; // Ensure null termination
    NotificationRenderer::alertBannerUntil = (durationMs == 0) ? 0 : millis() + durationMs;
    NotificationRenderer::alertBannerCallback = bannerCallback;
    NotificationRenderer::pauseBanner = false;
    NotificationRenderer::curSelected = 0;
    NotificationRenderer::current_notification_type = notificationTypeEnum::node_picker;

    static OverlayCallback overlays[] = {graphics::UIRenderer::drawNavigationBar, NotificationRenderer::drawBannercallback};
    ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));
    ui->setTargetFPS(60);
    ui->update();
}

// Called to trigger a banner with custom message and duration
void Screen::showNumberPicker(const char *message, uint32_t durationMs, uint8_t digits,
                              std::function<void(uint32_t)> bannerCallback)
{
#ifdef USE_EINK
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // Skip full refresh for all overlay menus
#endif
    // Store the message and set the expiration timestamp
    strncpy(NotificationRenderer::alertBannerMessage, message, 255);
    NotificationRenderer::alertBannerMessage[255] = '\0'; // Ensure null termination
    NotificationRenderer::alertBannerUntil = (durationMs == 0) ? 0 : millis() + durationMs;
    NotificationRenderer::alertBannerCallback = bannerCallback;
    NotificationRenderer::pauseBanner = false;
    NotificationRenderer::curSelected = 0;
    NotificationRenderer::current_notification_type = notificationTypeEnum::number_picker;
    NotificationRenderer::numDigits = digits;
    NotificationRenderer::currentNumber = 0;

    static OverlayCallback overlays[] = {graphics::UIRenderer::drawNavigationBar, NotificationRenderer::drawBannercallback};
    ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));
    ui->setTargetFPS(60);
    ui->update();
}

void Screen::showTextInput(const char *header, const char *initialText, uint32_t durationMs,
                           std::function<void(const std::string &)> textCallback)
{
    LOG_INFO("showTextInput called with header='%s', durationMs=%d", header ? header : "NULL", durationMs);

	// Remember current frame to return after sending
	if (ui && ui->getUiState()) {
		s_returnToFrame    = ui->getUiState()->currentFrame;
		s_reFocusAfterSend = true;
	}

    if (NotificationRenderer::virtualKeyboard) {
        delete NotificationRenderer::virtualKeyboard;
        NotificationRenderer::virtualKeyboard = nullptr;
    }

    NotificationRenderer::textInputCallback = nullptr;

    NotificationRenderer::virtualKeyboard = new VirtualKeyboard();
    if (header) {
        NotificationRenderer::virtualKeyboard->setHeader(header);
    }
    if (initialText) {
        NotificationRenderer::virtualKeyboard->setInputText(initialText);
    }

    // === Apply pending header here (last) ===
    if (!g_pendingKeyboardHeader.empty()) {
        std::string hdr = g_pendingKeyboardHeader;

        // limit of 11 to not overlap with "xxxleft"
        const int cap = 10;

        if ((int)hdr.size() > cap) {
            static GlobalScrollState g_headerScroll;
            std::string view = marqueeSlice(hdr, g_headerScroll, cap, true);
            NotificationRenderer::virtualKeyboard->setHeader(view.c_str());

            // keep header so scrolling continues
            g_chatScrollActive = true;
        } else {
            NotificationRenderer::virtualKeyboard->setHeader(hdr.c_str());
            // only clear if short, no longer needed
            g_pendingKeyboardHeader.clear();
        }
    }

    // Envolver el envío para volver al chat y evitar salto a “home”
auto wrappedSend = [this, textCallback](const std::string &text) {
    // 1) send (done by original callback)
    textCallback(text);

    // 2) return immediately to the chat frame we had
    if (s_returnToFrame >= 0) {
        ui->switchToFrame((uint8_t)s_returnToFrame);
        setFastFramerate();
        forceDisplay(true);

        // 3) mark refocus in case another setFrames occurs after
        s_reFocusAfterSend = true;
    }
};

NotificationRenderer::textInputCallback = wrappedSend;
NotificationRenderer::virtualKeyboard->setCallback(wrappedSend);


    strncpy(NotificationRenderer::alertBannerMessage, header ? header : "Text Input", 255);
    NotificationRenderer::alertBannerMessage[255] = '\0';
    NotificationRenderer::alertBannerUntil = (durationMs == 0) ? 0 : millis() + durationMs;
    NotificationRenderer::pauseBanner = false;
    NotificationRenderer::current_notification_type = notificationTypeEnum::text_input;

    // Set the overlay using the same pattern as other notification types
    static OverlayCallback overlays[] = {graphics::UIRenderer::drawNavigationBar, NotificationRenderer::drawBannercallback};
    ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));
    ui->setTargetFPS(60);
    ui->update();
}

static void drawModuleFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    uint8_t module_frame;
    // there's a little but in the UI transition code
    // where it invokes the function at the correct offset
    // in the array of "drawScreen" functions; however,
    // the passed-state doesn't quite reflect the "current"
    // screen, so we have to detect it.
    if (state->frameState == IN_TRANSITION && state->transitionFrameRelationship == TransitionRelationship_INCOMING) {
        // if we're transitioning from the end of the frame list back around to the first
        // frame, then we want this to be `0`
        module_frame = state->transitionFrameTarget;
    } else {
        // otherwise, just display the module frame that's aligned with the current frame
        module_frame = state->currentFrame;
        // LOG_DEBUG("Screen is not in transition.  Frame: %d", module_frame);
    }
    // LOG_DEBUG("Draw Module Frame %d", module_frame);
    MeshModule &pi = *moduleFrames.at(module_frame);
    pi.drawFrame(display, state, x, y);
}

// Ignore messages originating from phone (from the current node 0x0) unless range test or store and forward module are enabled
static bool shouldDrawMessage(const meshtastic_MeshPacket *packet)
{
    return packet->from != 0 && !moduleConfig.store_forward.enabled;
}

/**
 * Given a recent lat/lon return a guess of the heading the user is walking on.
 *
 * We keep a series of "after you've gone 10 meters, what is your heading since
 * the last reference point?"
 */
float Screen::estimatedHeading(double lat, double lon)
{
    static double oldLat, oldLon;
    static float b;

    if (oldLat == 0) {
        // just prepare for next time
        oldLat = lat;
        oldLon = lon;

        return b;
    }

    float d = GeoCoord::latLongToMeter(oldLat, oldLon, lat, lon);
    if (d < 10) // haven't moved enough, just keep current bearing
        return b;

    b = GeoCoord::bearing(oldLat, oldLon, lat, lon) * RAD_TO_DEG;
    oldLat = lat;
    oldLon = lon;

    return b;
}

/// We will skip one node - the one for us, so we just blindly loop over all
/// nodes
static int8_t prevFrame = -1;

// Combined dynamic node list frame cycling through LastHeard, HopSignal, and Distance modes
// Uses a single frame and changes data every few seconds (E-Ink variant is separate)

#if defined(ESP_PLATFORM) && defined(USE_ST7789)
SPIClass SPI1(HSPI);
#endif

Screen::Screen(ScanI2C::DeviceAddress address, meshtastic_Config_DisplayConfig_OledType screenType, OLEDDISPLAY_GEOMETRY geometry)
    : concurrency::OSThread("Screen"), address_found(address), model(screenType), geometry(geometry), cmdQueue(32)
{
    graphics::normalFrames = new FrameCallback[MAX_NUM_NODES + NUM_EXTRA_FRAMES];

    LOG_INFO("Protobuf Value uiconfig.screen_rgb_color: %d", uiconfig.screen_rgb_color);
    int32_t rawRGB = uiconfig.screen_rgb_color;
    if (rawRGB > 0 && rawRGB <= 255255255) {
        uint8_t TFT_MESH_r = (rawRGB >> 16) & 0xFF;
        uint8_t TFT_MESH_g = (rawRGB >> 8) & 0xFF;
        uint8_t TFT_MESH_b = rawRGB & 0xFF;
        LOG_INFO("Values of r,g,b: %d, %d, %d", TFT_MESH_r, TFT_MESH_g, TFT_MESH_b);

        if (TFT_MESH_r <= 255 && TFT_MESH_g <= 255 && TFT_MESH_b <= 255) {
            TFT_MESH = COLOR565(TFT_MESH_r, TFT_MESH_g, TFT_MESH_b);
        }
    }

#if defined(USE_SH1106) || defined(USE_SH1107) || defined(USE_SH1107_128_64)
    dispdev = new SH1106Wire(address.address, -1, -1, geometry,
                             (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif defined(USE_ST7789)
#ifdef ESP_PLATFORM
    dispdev = new ST7789Spi(&SPI1, ST7789_RESET, ST7789_RS, ST7789_NSS, GEOMETRY_RAWMODE, TFT_WIDTH, TFT_HEIGHT, ST7789_SDA,
                            ST7789_MISO, ST7789_SCK);
#else
    dispdev = new ST7789Spi(&SPI1, ST7789_RESET, ST7789_RS, ST7789_NSS, GEOMETRY_RAWMODE, TFT_WIDTH, TFT_HEIGHT);
#endif
    static_cast<ST7789Spi *>(dispdev)->setRGB(TFT_MESH);
#elif defined(USE_SSD1306)
    dispdev = new SSD1306Wire(address.address, -1, -1, geometry,
                              (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif defined(USE_SPISSD1306)
    dispdev = new SSD1306Spi(SSD1306_RESET, SSD1306_RS, SSD1306_NSS, GEOMETRY_64_48);
    if (!dispdev->init()) {
        LOG_DEBUG("Error: SSD1306 not detected!");
    } else {
        static_cast<SSD1306Spi *>(dispdev)->setHorizontalOffset(32);
        LOG_INFO("SSD1306 init success");
    }
#elif defined(ST7735_CS) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7701_CS) || defined(ST7789_CS) ||    \
    defined(RAK14014) || defined(HX8357_CS) || defined(ILI9488_CS) || defined(ST7796_CS)
    dispdev = new TFTDisplay(address.address, -1, -1, geometry,
                             (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif defined(USE_EINK) && !defined(USE_EINK_DYNAMICDISPLAY)
    dispdev = new EInkDisplay(address.address, -1, -1, geometry,
                              (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif defined(USE_EINK) && defined(USE_EINK_DYNAMICDISPLAY)
    dispdev = new EInkDynamicDisplay(address.address, -1, -1, geometry,
                                     (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif defined(USE_ST7567)
    dispdev = new ST7567Wire(address.address, -1, -1, geometry,
                             (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
#elif ARCH_PORTDUINO
    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_COLOR) {
        if (portduino_config.displayPanel != no_screen) {
            LOG_DEBUG("Make TFTDisplay!");
            dispdev = new TFTDisplay(address.address, -1, -1, geometry,
                                     (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
        } else {
            dispdev = new AutoOLEDWire(address.address, -1, -1, geometry,
                                       (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
            isAUTOOled = true;
        }
    }
#else
    dispdev = new AutoOLEDWire(address.address, -1, -1, geometry,
                               (address.port == ScanI2C::I2CPort::WIRE1) ? HW_I2C::I2C_TWO : HW_I2C::I2C_ONE);
    isAUTOOled = true;
#endif

    ui = new OLEDDisplayUi(dispdev);
    cmdQueue.setReader(this);
}

Screen::~Screen()
{
    delete[] graphics::normalFrames;
}

/**
 * Prepare the display for the unit going to the lowest power mode possible.  Most screens will just
 * poweroff, but eink screens will show a "I'm sleeping" graphic, possibly with a QR code
 */
void Screen::doDeepSleep()
{
#ifdef USE_EINK
    setOn(false, graphics::UIRenderer::drawDeepSleepFrame);
#else
    // Without E-Ink display:
    setOn(false);
#endif
}

void Screen::handleSetOn(bool on, FrameCallback einkScreensaver)
{
    if (!useDisplay)
        return;

    if (on != screenOn) {
        if (on) {
            LOG_INFO("Turn on screen");
            powerMon->setState(meshtastic_PowerMon_State_Screen_On);
#ifdef T_WATCH_S3
            PMU->enablePowerOutput(XPOWERS_ALDO2);
#endif

#if !ARCH_PORTDUINO
            dispdev->displayOn();
#endif

#ifdef PIN_EINK_EN
            if (uiconfig.screen_brightness == 1)
                digitalWrite(PIN_EINK_EN, HIGH);
#elif defined(PCA_PIN_EINK_EN)
            if (uiconfig.screen_brightness == 1)
                io.digitalWrite(PCA_PIN_EINK_EN, HIGH);
#endif

#if defined(ST7789_CS) &&                                                                                                        \
    !defined(M5STACK) // set display brightness when turning on screens. Just moved function from TFTDisplay to here.
            static_cast<TFTDisplay *>(dispdev)->setDisplayBrightness(brightness);
#endif

            dispdev->displayOn();
#ifdef HELTEC_TRACKER_V1_X
            ui->init();
#endif
#ifdef USE_ST7789
            pinMode(VTFT_CTRL, OUTPUT);
            digitalWrite(VTFT_CTRL, LOW);
            ui->init();
#ifdef ESP_PLATFORM
            analogWrite(VTFT_LEDA, BRIGHTNESS_DEFAULT);
#else
            pinMode(VTFT_LEDA, OUTPUT);
            digitalWrite(VTFT_LEDA, TFT_BACKLIGHT_ON);
#endif
#endif
            enabled = true;
            setInterval(0); // Draw ASAP
            runASAP = true;
        } else {
            powerMon->clearState(meshtastic_PowerMon_State_Screen_On);
#ifdef USE_EINK
            // eInkScreensaver parameter is usually NULL (default argument), default frame used instead
            setScreensaverFrames(einkScreensaver);
#endif

#ifdef PIN_EINK_EN
            digitalWrite(PIN_EINK_EN, LOW);
#elif defined(PCA_PIN_EINK_EN)
            io.digitalWrite(PCA_PIN_EINK_EN, LOW);
#endif

            dispdev->displayOff();
#ifdef USE_ST7789
            SPI1.end();
#if defined(ARCH_ESP32)
            pinMode(VTFT_LEDA, ANALOG);
            pinMode(VTFT_CTRL, ANALOG);
            pinMode(ST7789_RESET, ANALOG);
            pinMode(ST7789_RS, ANALOG);
            pinMode(ST7789_NSS, ANALOG);
#else
            nrf_gpio_cfg_default(VTFT_LEDA);
            nrf_gpio_cfg_default(VTFT_CTRL);
            nrf_gpio_cfg_default(ST7789_RESET);
            nrf_gpio_cfg_default(ST7789_RS);
            nrf_gpio_cfg_default(ST7789_NSS);
#endif
#endif

#ifdef T_WATCH_S3
            PMU->disablePowerOutput(XPOWERS_ALDO2);
#endif
            enabled = false;
        }
        screenOn = on;
    }
}

void Screen::setup()
{

    // === Enable display rendering ===
    useDisplay = true;

    // === Load saved brightness from UI config ===
    // For OLED displays (SSD1306), default brightness is 255 if not set
    if (uiconfig.screen_brightness == 0) {
#if defined(USE_OLED) || defined(USE_SSD1306) || defined(USE_SH1106) || defined(USE_SH1107)
        brightness = 255; // Default for OLED
#else
        brightness = BRIGHTNESS_DEFAULT;
#endif
    } else {
        brightness = uiconfig.screen_brightness;
    }

    // === Detect OLED subtype (if supported by board variant) ===
#ifdef AutoOLEDWire_h
    if (isAUTOOled)
        static_cast<AutoOLEDWire *>(dispdev)->setDetected(model);
#endif

#ifdef USE_SH1107_128_64
    static_cast<SH1106Wire *>(dispdev)->setSubtype(7);
#endif

#if defined(USE_ST7789) && defined(TFT_MESH)
    // Apply custom RGB color (e.g. Heltec T114/T190)
    static_cast<ST7789Spi *>(dispdev)->setRGB(TFT_MESH);
#endif

    // === Initialize display and UI system ===
    ui->init();
    displayWidth = dispdev->width();
    displayHeight = dispdev->height();

    ui->setTimePerTransition(0);           // Disable animation delays
    ui->setIndicatorPosition(BOTTOM);      // Not used (indicators disabled below)
    ui->setIndicatorDirection(LEFT_RIGHT); // Not used (indicators disabled below)
    ui->setFrameAnimation(SLIDE_LEFT);     // Used only when indicators are active
    ui->disableAllIndicators();            // Disable page indicator dots
    ui->getUiState()->userData = this;     // Allow static callbacks to access Screen instance

    // === Apply loaded brightness ===
#if defined(ST7789_CS)
    static_cast<TFTDisplay *>(dispdev)->setDisplayBrightness(brightness);
#elif defined(USE_OLED) || defined(USE_SSD1306) || defined(USE_SH1106) || defined(USE_SH1107) || defined(USE_SPISSD1306)
    dispdev->setBrightness(brightness);
#endif
    LOG_INFO("Applied screen brightness: %d", brightness);

    // === Set custom overlay callbacks ===
    static OverlayCallback overlays[] = {
        graphics::UIRenderer::drawNavigationBar // Custom indicator icons for each frame
    };
    ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));

    // === Enable UTF-8 to display mapping ===
    dispdev->setFontTableLookupFunction(customFontTableLookup);

#ifdef USERPREFS_OEM_TEXT
    logo_timeout *= 2; // Give more time for branded boot logos
#endif

    // === Configure alert frames (e.g., "Resuming..." or region name) ===
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // Skip slow refresh
    alertFrames[0] = [this](OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
#ifdef ARCH_ESP32
        if (wakeCause == ESP_SLEEP_WAKEUP_TIMER || wakeCause == ESP_SLEEP_WAKEUP_EXT1)
            graphics::UIRenderer::drawFrameText(display, state, x, y, "Resuming...");
        else
#endif
        {
            const char *region = myRegion ? myRegion->name : nullptr;
            graphics::UIRenderer::drawIconScreen(region, display, state, x, y);
        }
    };
    ui->setFrames(alertFrames, 1);
    ui->disableAutoTransition(); // Require manual navigation between frames

    // === Log buffer for on-screen logs (3 lines max) ===
    dispdev->setLogBuffer(3, 32);

    // === Optional screen mirroring or flipping (e.g. for T-Beam orientation) ===
#ifdef SCREEN_MIRROR
    dispdev->mirrorScreen();
#else
    if (!config.display.flip_screen) {
#if defined(ST7701_CS) || defined(ST7735_CS) || defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(ST7789_CS) ||      \
    defined(RAK14014) || defined(HX8357_CS) || defined(ILI9488_CS) || defined(ST7796_CS)
        static_cast<TFTDisplay *>(dispdev)->flipScreenVertically();
#elif defined(USE_ST7789)
        static_cast<ST7789Spi *>(dispdev)->flipScreenVertically();
#elif !defined(M5STACK_UNITC6L)
        dispdev->flipScreenVertically();
#endif
    }
#endif

    // === Generate device ID from MAC address ===
    uint8_t dmac[6];
    getMacAddr(dmac);
    snprintf(screen->ourId, sizeof(screen->ourId), "%02x%02x", dmac[4], dmac[5]);

#if ARCH_PORTDUINO
    handleSetOn(false); // Ensure proper init for Arduino targets
#endif

    // === Turn on display and trigger first draw ===
    handleSetOn(true);
    determineResolution(dispdev->height(), dispdev->width());
    ui->update();
#ifndef USE_EINK
    ui->update(); // Some SSD1306 clones drop the first draw, so run twice
#endif
    serialSinceMsec = millis();

#if ARCH_PORTDUINO
    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_COLOR) {
        if (portduino_config.touchscreenModule) {
            touchScreenImpl1 =
                new TouchScreenImpl1(dispdev->getWidth(), dispdev->getHeight(), static_cast<TFTDisplay *>(dispdev)->getTouch);
            touchScreenImpl1->init();
        }
    }
#elif HAS_TOUCHSCREEN && !defined(USE_EINK)
    touchScreenImpl1 =
        new TouchScreenImpl1(dispdev->getWidth(), dispdev->getHeight(), static_cast<TFTDisplay *>(dispdev)->getTouch);
    touchScreenImpl1->init();
#endif

    // === Subscribe to device status updates ===
    powerStatusObserver.observe(&powerStatus->onNewStatus);
    gpsStatusObserver.observe(&gpsStatus->onNewStatus);
    nodeStatusObserver.observe(&nodeStatus->onNewStatus);

#if !MESHTASTIC_EXCLUDE_ADMIN
    adminMessageObserver.observe(adminModule);
#endif
    if (textMessageModule)
        textMessageObserver.observe(textMessageModule);
    if (inputBroker)
        inputObserver.observe(inputBroker);

    // === Notify modules that support UI events ===
    MeshModule::observeUIEvents(&uiFrameEventObserver);
}

void Screen::forceDisplay(bool forceUiUpdate)
{
    // Nasty hack to force epaper updates for 'key' frames.  FIXME, cleanup.
#ifdef USE_EINK
    // If requested, make sure queued commands are run, and UI has rendered a new frame
    if (forceUiUpdate) {
        // Force a display refresh, in addition to the UI update
        // Changing the GPS status bar icon apparently doesn't register as a change in image
        // (False negative of the image hashing algorithm used to skip identical frames)
        EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST);

        // No delay between UI frame rendering
        setFastFramerate();

        // Make sure all CMDs have run first
        while (!cmdQueue.isEmpty())
            runOnce();

        // Ensure at least one frame has drawn
        uint64_t startUpdate;
        do {
            startUpdate = millis(); // Handle impossibly unlikely corner case of a millis() overflow..
            delay(10);
            ui->update();
        } while (ui->getUiState()->lastUpdate < startUpdate);

        // Return to normal frame rate
        targetFramerate = IDLE_FRAMERATE;
        ui->setTargetFPS(targetFramerate);
    }

    // Tell EInk class to update the display
    static_cast<EInkDisplay *>(dispdev)->forceDisplay();
#else
    // No delay between UI frame rendering
    if (forceUiUpdate) {
        setFastFramerate();
    }
#endif
}

static uint32_t lastScreenTransition;

int32_t Screen::runOnce()
{
    // If we don't have a screen, don't ever spend any CPU for us.
    if (!useDisplay) {
        enabled = false;
        return RUN_SAME;
    }

    if (displayHeight == 0) {
        displayHeight = dispdev->getHeight();
    }
    menuHandler::handleMenuSwitch(dispdev);

    // Show boot screen for first logo_timeout seconds, then switch to normal operation.
    // serialSinceMsec adjusts for additional serial wait time during nRF52 bootup
    static bool showingBootScreen = true;
    if (showingBootScreen && (millis() > (logo_timeout + serialSinceMsec))) {
        LOG_INFO("Done with boot screen");
        stopBootScreen();
        showingBootScreen = false;
    }

#ifdef USERPREFS_OEM_TEXT
    static bool showingOEMBootScreen = true;
    if (showingOEMBootScreen && (millis() > ((logo_timeout / 2) + serialSinceMsec))) {
        LOG_INFO("Switch to OEM screen...");
        // Change frames.
        static FrameCallback bootOEMFrames[] = {graphics::UIRenderer::drawOEMBootScreen};
        static const int bootOEMFrameCount = sizeof(bootOEMFrames) / sizeof(bootOEMFrames[0]);
        ui->setFrames(bootOEMFrames, bootOEMFrameCount);
        ui->update();
#ifndef USE_EINK
        ui->update();
#endif
        showingOEMBootScreen = false;
    }
#endif

#ifndef DISABLE_WELCOME_UNSET
    if (!NotificationRenderer::isOverlayBannerShowing() && config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET) {
#if defined(M5STACK_UNITC6L)
        menuHandler::LoraRegionPicker();
#else
        menuHandler::OnboardMessage();
#endif
    }
#endif
    if (!NotificationRenderer::isOverlayBannerShowing() && rebootAtMsec != 0) {
        showSimpleBanner("Rebooting...", 0);
    }

    // Process incoming commands.
    for (;;) {
        ScreenCmd cmd;
        if (!cmdQueue.dequeue(&cmd, 0)) {
            break;
        }
        switch (cmd.cmd) {
        case Cmd::SET_ON:
            handleSetOn(true);
            break;
        case Cmd::SET_OFF:
            handleSetOn(false);
            break;
        case Cmd::ON_PRESS:
            if (NotificationRenderer::current_notification_type != notificationTypeEnum::text_input) {
                handleOnPress();
            }
            break;
        case Cmd::SHOW_PREV_FRAME:
            if (NotificationRenderer::current_notification_type != notificationTypeEnum::text_input) {
                handleShowPrevFrame();
            }
            break;
        case Cmd::SHOW_NEXT_FRAME:
            if (NotificationRenderer::current_notification_type != notificationTypeEnum::text_input) {
                handleShowNextFrame();
            }
            break;
        case Cmd::START_ALERT_FRAME: {
            showingBootScreen = false; // this should avoid the edge case where an alert triggers before the boot screen goes away
            showingNormalScreen = false;
            NotificationRenderer::pauseBanner = true;
            alertFrames[0] = alertFrame;
#ifdef USE_EINK
            EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // Use fast-refresh for next frame, no skip please
            EINK_ADD_FRAMEFLAG(dispdev, BLOCKING);    // Edge case: if this frame is promoted to COSMETIC, wait for update
            handleSetOn(true); // Ensure power-on to receive deep-sleep screensaver (PowerFSM should handle?)
#endif
            setFrameImmediateDraw(alertFrames);
            break;
        }
        case Cmd::START_FIRMWARE_UPDATE_SCREEN:
            handleStartFirmwareUpdateScreen();
            break;
        case Cmd::STOP_ALERT_FRAME:
            NotificationRenderer::pauseBanner = false;
        case Cmd::STOP_BOOT_SCREEN:
            EINK_ADD_FRAMEFLAG(dispdev, COSMETIC); // E-Ink: Explicitly use full-refresh for next frame
            if (NotificationRenderer::current_notification_type != notificationTypeEnum::text_input) {
                setFrames();
            }
            break;
        case Cmd::NOOP:
            break;
        default:
            LOG_ERROR("Invalid screen cmd");
        }
    }

    if (!screenOn) { // If we didn't just wake and the screen is still off, then
                     // stop updating until it is on again
        enabled = false;
        return 0;
    }

    // this must be before the frameState == FIXED check, because we always
    // want to draw at least one FIXED frame before doing forceDisplay
    ui->update();

    // Manage FPS based on whether marquee is active or not
    if (ui->getUiState()->frameState == FIXED) {
        // Check for frame changes to reset scroll when entering chat
        checkFrameChange();

        // Check for inactivity timeouts (40s home return, 60s screen off)
        checkInactivityTimeouts();

        if (g_chatScrollActive) {
            if (targetFramerate == IDLE_FRAMERATE) {
                setFastFramerate();
            }
        } else if (targetFramerate != IDLE_FRAMERATE) {
            targetFramerate = IDLE_FRAMERATE;
            ui->setTargetFPS(targetFramerate);
            forceDisplay();
        }
    }

    // While showing the bootscreen or Bluetooth pair screen all of our
    // standard screen switching is stopped.
    if (showingNormalScreen) {
        // standard screen loop handling here
        if (config.display.auto_screen_carousel_secs > 0 &&
            NotificationRenderer::current_notification_type != notificationTypeEnum::text_input &&
            !Throttle::isWithinTimespanMs(lastScreenTransition, config.display.auto_screen_carousel_secs * 1000)) {

            // If an E-Ink display struggles with fast refresh, force carousel to use full refresh instead
            // Carousel is potentially a major source of E-Ink display wear
#if !defined(EINK_BACKGROUND_USES_FAST)
            EINK_ADD_FRAMEFLAG(dispdev, COSMETIC);
#endif

            LOG_DEBUG("LastScreenTransition exceeded %ums transition to next frame", (millis() - lastScreenTransition));
            handleOnPress();
        }
    }

    // LOG_DEBUG("want fps %d, fixed=%d", targetFramerate,
    // ui->getUiState()->frameState); If we are scrolling we need to be called
    // soon, otherwise just 1 fps (to save CPU) We also ask to be called twice
    // as fast as we really need so that any rounding errors still result with
    // the correct framerate
    return (1000 / targetFramerate);
}

/* show a message that the SSL cert is being built
 * it is expected that this will be used during the boot phase */
void Screen::setSSLFrames()
{
    if (address_found.address) {
        // LOG_DEBUG("Show SSL frames");
        static FrameCallback sslFrames[] = {NotificationRenderer::drawSSLScreen};
        ui->setFrames(sslFrames, 1);
        ui->update();
    }
}

#ifdef USE_EINK
/// Determine which screensaver frame to use, then set the FrameCallback
void Screen::setScreensaverFrames(FrameCallback einkScreensaver)
{
    // Retain specified frame / overlay callback beyond scope of this method
    static FrameCallback screensaverFrame;
    static OverlayCallback screensaverOverlay;

#if defined(HAS_EINK_ASYNCFULL) && defined(USE_EINK_DYNAMICDISPLAY)
    // Join (await) a currently running async refresh, then run the post-update code.
    // Avoid skipping of screensaver frame. Would otherwise be handled by NotifiedWorkerThread.
    EINK_JOIN_ASYNCREFRESH(dispdev);
#endif

    // If: one-off screensaver frame passed as argument. Handles doDeepSleep()
    if (einkScreensaver != NULL) {
        screensaverFrame = einkScreensaver;
        ui->setFrames(&screensaverFrame, 1);
    }

    // Else, display the usual "overlay" screensaver
    else {
        screensaverOverlay = graphics::UIRenderer::drawScreensaverOverlay;
        ui->setOverlays(&screensaverOverlay, 1);
    }

    // Request new frame, ASAP
    setFastFramerate();
    uint64_t startUpdate;
    do {
        startUpdate = millis(); // Handle impossibly unlikely corner case of a millis() overflow..
        delay(1);
        ui->update();
    } while (ui->getUiState()->lastUpdate < startUpdate);

    // Old EInkDisplay class
#if !defined(USE_EINK_DYNAMICDISPLAY)
    static_cast<EInkDisplay *>(dispdev)->forceDisplay(0); // Screen::forceDisplay(), but override rate-limit
#endif

    // Prepare now for next frame, shown when display wakes
    ui->setOverlays(NULL, 0);  // Clear overlay
    setFrames(FOCUS_PRESERVE); // Return to normal display updates, showing same frame as before screensaver, ideally

    // Pick a refresh method, for when display wakes
#ifdef EINK_HASQUIRK_GHOSTING
    EINK_ADD_FRAMEFLAG(dispdev, COSMETIC); // Really ugly to see ghosting from "screen paused"
#else
    EINK_ADD_FRAMEFLAG(dispdev, RESPONSIVE); // Really nice to wake screen with a fast-refresh
#endif
}
#endif

// Regenerate the normal set of frames, focusing a specific frame if requested
// Called when a frame should be added / removed, or custom frames should be cleared
void Screen::setFrames(FrameFocus focus)
{
    // Block setFrames calls when virtual keyboard is active to prevent overlay interference
    if (NotificationRenderer::current_notification_type == notificationTypeEnum::text_input) {
        return;
    }

    uint8_t originalPosition = ui->getUiState()->currentFrame;
    uint8_t previousFrameCount = framesetInfo.frameCount;
    FramesetInfo fsi; // Location of specific frames, for applying focus parameter

    graphics::UIRenderer::rebuildFavoritedNodes();

    LOG_DEBUG("Show standard frames");
    showingNormalScreen = true;

    indicatorIcons.clear();

    size_t numframes = 0;

    // If we have a critical fault, show it first
    fsi.positions.fault = numframes;
    if (error_code) {
        normalFrames[numframes++] = NotificationRenderer::drawCriticalFaultFrame;
        indicatorIcons.push_back(icon_error);
        focus = FOCUS_FAULT; // Change our "focus" parameter, to ensure we show the fault frame
    }

#if defined(DISPLAY_CLOCK_FRAME)
    if (!hiddenFrames.clock) {
        fsi.positions.clock = numframes;
#if defined(M5STACK_UNITC6L)
        normalFrames[numframes++] = graphics::ClockRenderer::drawAnalogClockFrame;
#else
        normalFrames[numframes++] = uiconfig.is_clockface_analog ? graphics::ClockRenderer::drawAnalogClockFrame
                                                                 : graphics::ClockRenderer::drawDigitalClockFrame;
#endif
        indicatorIcons.push_back(digital_icon_clock);
    }
#endif


    if (!hiddenFrames.home) {
        fsi.positions.home = numframes;
        normalFrames[numframes++] = graphics::UIRenderer::drawDeviceFocused;
        indicatorIcons.push_back(icon_home);
    }


#ifndef USE_EINK
    if (!hiddenFrames.nodelist) {
        fsi.positions.nodelist = numframes;
        normalFrames[numframes++] = graphics::NodeListRenderer::drawDynamicNodeListScreen;
        indicatorIcons.push_back(icon_nodes);
    }
#endif

// Show detailed node views only on E-Ink builds
#ifdef USE_EINK
    if (!hiddenFrames.nodelist_lastheard) {
        fsi.positions.nodelist_lastheard = numframes;
        normalFrames[numframes++] = graphics::NodeListRenderer::drawLastHeardScreen;
        indicatorIcons.push_back(icon_nodes);
    }
    if (!hiddenFrames.nodelist_hopsignal) {
        fsi.positions.nodelist_hopsignal = numframes;
        normalFrames[numframes++] = graphics::NodeListRenderer::drawHopSignalScreen;
        indicatorIcons.push_back(icon_signal);
    }
    if (!hiddenFrames.nodelist_distance) {
        fsi.positions.nodelist_distance = numframes;
        normalFrames[numframes++] = graphics::NodeListRenderer::drawDistanceScreen;
        indicatorIcons.push_back(icon_distance);
    }
#endif
#if HAS_GPS
    if (!hiddenFrames.nodelist_bearings) {
        fsi.positions.nodelist_bearings = numframes;
        normalFrames[numframes++] = graphics::NodeListRenderer::drawNodeListWithCompasses;
        indicatorIcons.push_back(icon_list);
    }
    if (!hiddenFrames.gps) {
        fsi.positions.gps = numframes;
        normalFrames[numframes++] = graphics::UIRenderer::drawCompassAndLocationScreen;
        indicatorIcons.push_back(icon_compass);
    }
#endif
    if (RadioLibInterface::instance && !hiddenFrames.lora) {
        fsi.positions.lora = numframes;
        normalFrames[numframes++] = graphics::DebugRenderer::drawLoRaFocused;
        indicatorIcons.push_back(icon_radio);
    }
    if (!hiddenFrames.system) {
        fsi.positions.system = numframes;
        normalFrames[numframes++] = graphics::DebugRenderer::drawSystemScreen;
        indicatorIcons.push_back(icon_system);
    }
#if !defined(DISPLAY_CLOCK_FRAME)
    if (!hiddenFrames.clock) {
        fsi.positions.clock = numframes;
        normalFrames[numframes++] = uiconfig.is_clockface_analog ? graphics::ClockRenderer::drawAnalogClockFrame
                                                                 : graphics::ClockRenderer::drawDigitalClockFrame;
        indicatorIcons.push_back(digital_icon_clock);
    }
#endif
    if (!hiddenFrames.chirpy) {
        fsi.positions.chirpy = numframes;
        normalFrames[numframes++] = graphics::DebugRenderer::drawChirpy;
        indicatorIcons.push_back(chirpy_small);
    }

#if HAS_WIFI && !defined(ARCH_PORTDUINO)
    if (!hiddenFrames.wifi && isWifiAvailable()) {
        fsi.positions.wifi = numframes;
        normalFrames[numframes++] = graphics::DebugRenderer::drawDebugInfoWiFiTrampoline;
        indicatorIcons.push_back(icon_wifi);
    }
#endif

    // Beware of what changes you make in this code!
    // We pass numframes into GetMeshModulesWithUIFrames() which is highly important!
    // Inside of that callback, goes over to MeshModule.cpp and we run
    // modulesWithUIFrames.resize(startIndex, nullptr), to insert nullptr
    // entries until we're ready to start building the matching entries.
    // We are doing our best to keep the normalFrames vector
    // and the moduleFrames vector in lock step.
    moduleFrames = MeshModule::GetMeshModulesWithUIFrames(numframes);
    LOG_DEBUG("Show %d module frames", moduleFrames.size());

    for (auto i = moduleFrames.begin(); i != moduleFrames.end(); ++i) {
        // Draw the module frame, using the hack described above
        if (*i != nullptr) {
            normalFrames[numframes] = drawModuleFrame;

            // Check if the module being drawn has requested focus
            // We will honor this request later, if setFrames was triggered by a UIFrameEvent
            MeshModule *m = *i;
            if (m && m->isRequestingFocus())
                fsi.positions.focusedModule = numframes;
            if (m && m == waypointModule)
                fsi.positions.waypoint = numframes;

            indicatorIcons.push_back(icon_module);
            numframes++;
        }
    }

    LOG_DEBUG("Added modules.  numframes: %d", numframes);
    // --- seed channel tabs at startup ---
    // This ensures that favorite channels are available as tabs when the UI loads.
    seedChannelTabsFromConfig();


    // ===== Chat tabs by node (favorites) =====
    {
        graphics::g_favChatNodes.clear();
        for (size_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
            const meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(i);
            if (n && n->num != nodeDB->getNodeNum() && n->is_favorite) {
                graphics::g_favChatNodes.push_back(n->num);
            }
        }
        if (!graphics::g_favChatNodes.empty()) {
            graphics::g_favChatFirst = numframes;
            for (size_t i = 0; i < graphics::g_favChatNodes.size(); ++i) {
                normalFrames[numframes++] = graphics::drawFavNodeChatFrame;
                indicatorIcons.push_back(icon_mail);
            }
            graphics::g_favChatLast = numframes - 1;
        } else {
            graphics::g_favChatFirst = graphics::g_favChatLast = (size_t)-1;
        }
    }

    // ===== Chat tabs by channel =====
    {
        using chat::ChatHistoryStore;
        auto &store = ChatHistoryStore::instance();

    // Merge channel history with favorites managed here
    // This ensures that both recently used and favorite channels appear as tabs
    std::set<uint8_t> combined;
    std::vector<uint8_t> fromHistory = store.listChannels();
    combined.insert(fromHistory.begin(), fromHistory.end());
    combined.insert(g_favChannelTabs.begin(), g_favChannelTabs.end());

        graphics::g_chanTabs.assign(combined.begin(), combined.end());

        if (!graphics::g_chanTabs.empty()) {
            graphics::g_chanTabFirst = numframes;
            for (size_t i = 0; i < graphics::g_chanTabs.size(); ++i) {
                normalFrames[numframes++] = graphics::drawChannelChatTabFrame;
                indicatorIcons.push_back(icon_mail);
            }
            graphics::g_chanTabLast = numframes - 1;
        } else {
            graphics::g_chanTabFirst = graphics::g_chanTabLast = (size_t)-1;
        }
    }

    fsi.frameCount = numframes;   // Total framecount is used to apply FOCUS_PRESERVE
    this->frameCount = numframes; // ✅ Save frame count for use in custom overlay
    LOG_DEBUG("Finished build frames. numframes: %d", numframes);

    ui->setFrames(normalFrames, numframes);
    ui->disableAllIndicators();

    // Add overlays: frame icons and alert banner)
    static OverlayCallback overlays[] = {graphics::UIRenderer::drawNavigationBar, NotificationRenderer::drawBannercallback};
    ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));

    prevFrame = -1; // Force drawNodeInfo to pick a new node (because our list just changed)

    // Focus on a specific frame, in the frame set we just created
    switch (focus) {
    case FOCUS_DEFAULT:
        ui->switchToFrame(fsi.positions.deviceFocused);
        break;
    case FOCUS_FAULT:
        ui->switchToFrame(fsi.positions.fault);
        break;
    case FOCUS_TEXTMESSAGE:
        hasUnreadMessage = false; // ✅ Clear when message is *viewed*
        ui->switchToFrame(fsi.positions.textMessage);
        break;
    case FOCUS_MODULE:
        // Whichever frame was marked by MeshModule::requestFocus(), if any
        // If no module requested focus, will show the first frame instead
        ui->switchToFrame(fsi.positions.focusedModule);
        break;
    case FOCUS_CLOCK:
        // Whichever frame was marked by MeshModule::requestFocus(), if any
        // If no module requested focus, will show the first frame instead
        ui->switchToFrame(fsi.positions.clock);
        break;
    case FOCUS_SYSTEM:
        ui->switchToFrame(fsi.positions.system);
        break;

    case FOCUS_PRESERVE:
        //  No more adjustment — force stay on same index
        if (previousFrameCount > fsi.frameCount) {
            ui->switchToFrame(originalPosition - 1);
        } else if (previousFrameCount < fsi.frameCount) {
            ui->switchToFrame(originalPosition + 1);
        } else {
            ui->switchToFrame(originalPosition);
        }
        break;
    }

    // Store the info about this frameset, for future setFrames calls
    this->framesetInfo = fsi;


if (s_reFocusAfterSend && s_returnToFrame >= 0) {
    uint8_t target = (uint8_t)std::min<int>(s_returnToFrame, (int)frameCount - 1);
    ui->switchToFrame(target);
    s_reFocusAfterSend = false;
    s_returnToFrame    = -1;
}

    setFastFramerate(); // Draw ASAP
}

void Screen::setFrameImmediateDraw(FrameCallback *drawFrames)
{
    ui->disableAllIndicators();
    ui->setFrames(drawFrames, 1);
    setFastFramerate();
}

void Screen::toggleFrameVisibility(const std::string &frameName)
{
#ifndef USE_EINK
    if (frameName == "nodelist") {
        hiddenFrames.nodelist = !hiddenFrames.nodelist;
    }
#endif
#ifdef USE_EINK
    if (frameName == "nodelist_lastheard") {
        hiddenFrames.nodelist_lastheard = !hiddenFrames.nodelist_lastheard;
    }
    if (frameName == "nodelist_hopsignal") {
        hiddenFrames.nodelist_hopsignal = !hiddenFrames.nodelist_hopsignal;
    }
    if (frameName == "nodelist_distance") {
        hiddenFrames.nodelist_distance = !hiddenFrames.nodelist_distance;
    }
#endif
#if HAS_GPS
    if (frameName == "nodelist_bearings") {
        hiddenFrames.nodelist_bearings = !hiddenFrames.nodelist_bearings;
    }
    if (frameName == "gps") {
        hiddenFrames.gps = !hiddenFrames.gps;
    }
#endif
    if (frameName == "lora") {
        hiddenFrames.lora = !hiddenFrames.lora;
    }
    if (frameName == "clock") {
        hiddenFrames.clock = !hiddenFrames.clock;
    }
    if (frameName == "show_favorites") {
        hiddenFrames.show_favorites = !hiddenFrames.show_favorites;
    }
    if (frameName == "chirpy") {
        hiddenFrames.chirpy = !hiddenFrames.chirpy;
    }
}

bool Screen::isFrameHidden(const std::string &frameName) const
{
#ifndef USE_EINK
    if (frameName == "nodelist")
        return hiddenFrames.nodelist;
#endif
#ifdef USE_EINK
    if (frameName == "nodelist_lastheard")
        return hiddenFrames.nodelist_lastheard;
    if (frameName == "nodelist_hopsignal")
        return hiddenFrames.nodelist_hopsignal;
    if (frameName == "nodelist_distance")
        return hiddenFrames.nodelist_distance;
#endif
#if HAS_GPS
    if (frameName == "nodelist_bearings")
        return hiddenFrames.nodelist_bearings;
    if (frameName == "gps")
        return hiddenFrames.gps;
#endif
    if (frameName == "lora")
        return hiddenFrames.lora;
    if (frameName == "clock")
        return hiddenFrames.clock;
    if (frameName == "show_favorites")
        return hiddenFrames.show_favorites;
    if (frameName == "chirpy")
        return hiddenFrames.chirpy;

    return false;
}

// Dismisses the currently displayed screen frame, if possible
// Relevant for text message, waypoint, others in future?
// Triggered with a CardKB keycombo
void Screen::hideCurrentFrame()
{
    uint8_t currentFrame = ui->getUiState()->currentFrame;
    bool dismissed = false;
    if (currentFrame == framesetInfo.positions.textMessage && devicestate.has_rx_text_message) {
        LOG_INFO("Hide Text Message");
        devicestate.has_rx_text_message = false;
        memset(&devicestate.rx_text_message, 0, sizeof(devicestate.rx_text_message));
        hiddenFrames.textMessage = true;
        dismissed = true;
    } else if (currentFrame == framesetInfo.positions.waypoint && devicestate.has_rx_waypoint) {
        LOG_DEBUG("Hide Waypoint");
        devicestate.has_rx_waypoint = false;
        hiddenFrames.waypoint = true;
        dismissed = true;
    } else if (currentFrame == framesetInfo.positions.wifi) {
        LOG_DEBUG("Hide WiFi Screen");
        hiddenFrames.wifi = true;
        dismissed = true;
    } else if (currentFrame == framesetInfo.positions.lora) {
        LOG_INFO("Hide LoRa");
        hiddenFrames.lora = true;
        dismissed = true;
    }

    if (dismissed) {
        setFrames(FOCUS_DEFAULT); // You could also use FOCUS_PRESERVE
    }
}

void Screen::handleStartFirmwareUpdateScreen()
{
    LOG_DEBUG("Show firmware screen");
    showingNormalScreen = false;
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // E-Ink: Explicitly use fast-refresh for next frame

    static FrameCallback frames[] = {graphics::NotificationRenderer::drawFrameFirmware};
    setFrameImmediateDraw(frames);
}

void Screen::blink()
{
    setFastFramerate();
    uint8_t count = 10;
    dispdev->setBrightness(254);
    while (count > 0) {
        dispdev->fillRect(0, 0, dispdev->getWidth(), dispdev->getHeight());
        dispdev->display();
        delay(50);
        dispdev->clear();
        dispdev->display();
        delay(50);
        count = count - 1;
    }
    // The dispdev->setBrightness does not work for t-deck display, it seems to run the setBrightness function in
    // OLEDDisplay.
    dispdev->setBrightness(brightness);
}

void Screen::increaseBrightness()
{
    brightness = ((brightness + 62) > 254) ? brightness : (brightness + 62);

#if defined(ST7789_CS)
    // run the setDisplayBrightness function. This works on t-decks
    static_cast<TFTDisplay *>(dispdev)->setDisplayBrightness(brightness);
#endif

    /* TO DO: add little popup in center of screen saying what brightness level it is set to*/
}

void Screen::decreaseBrightness()
{
    brightness = (brightness < 70) ? brightness : (brightness - 62);

#if defined(ST7789_CS)
    static_cast<TFTDisplay *>(dispdev)->setDisplayBrightness(brightness);
#endif

    /* TO DO: add little popup in center of screen saying what brightness level it is set to*/
}

void Screen::setFunctionSymbol(std::string sym)
{
    if (std::find(functionSymbol.begin(), functionSymbol.end(), sym) == functionSymbol.end()) {
        functionSymbol.push_back(sym);
        functionSymbolString = "";
        for (auto symbol : functionSymbol) {
            functionSymbolString = symbol + " " + functionSymbolString;
        }
        setFastFramerate();
    }
}

void Screen::removeFunctionSymbol(std::string sym)
{
    functionSymbol.erase(std::remove(functionSymbol.begin(), functionSymbol.end(), sym), functionSymbol.end());
    functionSymbolString = "";
    for (auto symbol : functionSymbol) {
        functionSymbolString = symbol + " " + functionSymbolString;
    }
    setFastFramerate();
}

void Screen::handleOnPress()
{
    // If screen was off, just wake it, otherwise advance to next frame
    // If we are in a transition, the press must have bounced, drop it.
    if (ui->getUiState()->frameState == FIXED) {
        ui->nextFrame();
        lastScreenTransition = millis();
        setFastFramerate();
    }
}

void Screen::handleShowPrevFrame()
{
    // If screen was off, just wake it, otherwise go back to previous frame
    // If we are in a transition, the press must have bounced, drop it.
    if (ui->getUiState()->frameState == FIXED) {
        ui->previousFrame();
        lastScreenTransition = millis();
        setFastFramerate();
    }
}

void Screen::handleShowNextFrame()
{
    // If screen was off, just wake it, otherwise advance to next frame
    // If we are in a transition, the press must have bounced, drop it.
    if (ui->getUiState()->frameState == FIXED) {
        ui->nextFrame();
        lastScreenTransition = millis();
        setFastFramerate();
    }
}

#ifndef SCREEN_TRANSITION_FRAMERATE
#define SCREEN_TRANSITION_FRAMERATE 30 // fps
#endif

void Screen::setFastFramerate()
{
#if defined(M5STACK_UNITC6L)
    dispdev->clear();
    dispdev->display();
#endif
    // We are about to start a transition so speed up fps
    targetFramerate = SCREEN_TRANSITION_FRAMERATE;

    ui->setTargetFPS(targetFramerate);
    setInterval(0); // redraw ASAP
    runASAP = true;
}

int Screen::handleStatusUpdate(const meshtastic::Status *arg)
{
    // LOG_DEBUG("Screen got status update %d", arg->getStatusType());
    switch (arg->getStatusType()) {
    case STATUS_TYPE_NODE:
        if (showingNormalScreen && nodeStatus->getLastNumTotal() != nodeStatus->getNumTotal()) {
            setFrames(FOCUS_PRESERVE); // Regen the list of screen frames (returning to same frame, if possible)
        }
        nodeDB->updateGUI = false;
        break;
    }

    return 0;
}

// Handles when message is received; will jump to text message frame.
int Screen::handleTextMessage(const meshtastic_MeshPacket *packet)
{
    if (showingNormalScreen) {
        if (packet->from == 0) {
            // Outgoing message (likely sent from phone)
            devicestate.has_rx_text_message = false;
            memset(&devicestate.rx_text_message, 0, sizeof(devicestate.rx_text_message));
            hiddenFrames.textMessage = true;
            hasUnreadMessage = false; // Clear unread state when user replies

            setFrames(FOCUS_PRESERVE); // Stay on same frame, silently update frame list
        } else {
            // === FAVORITES: only in DM (destination = my NodeNum) ===
            // If the message is direct, mark the sender as favorite for quick access in chat tabs
            const bool isDirect = (nodeDB && packet->to == nodeDB->getNodeNum());
            if (isDirect) {
                const uint32_t fromId = packet->from;
                if (nodeDB && fromId != nodeDB->getNodeNum()) {
                    const meshtastic_NodeInfoLite *cn = nodeDB->getMeshNode(fromId);
                    bool isFav = (cn && cn->is_favorite);
                    if (!isFav) {
                        nodeDB->set_favorite(fromId, true);
                        if (cn) { const_cast<meshtastic_NodeInfoLite *>(cn)->is_favorite = true; }
                    }
                }
            } else {
                // Channel message: optionally mark the channel as internal favorite
                uint8_t ch = (uint8_t)packet->channel;
                g_favChannelTabs.insert(ch);
            }

            // Estado y refresco
            devicestate.has_rx_text_message = true;
            hasUnreadMessage = true;
            setFrames(FOCUS_PRESERVE);

            if (shouldWakeOnReceivedMessage()) setOn(true);

            // === SCREEN JUMP ===
            uint8_t jumpTo = 0xFF;
            if (isDirect) {
                if (g_favChatFirst != (size_t)-1) {
                    auto it = std::find(g_favChatNodes.begin(), g_favChatNodes.end(), packet->from);
                    if (it != g_favChatNodes.end()) {
                        jumpTo = (uint8_t)(g_favChatFirst + (it - g_favChatNodes.begin()));
                    }
                }
            } else {
                if (g_chanTabFirst != (size_t)-1) {
                    uint8_t ch = (uint8_t)packet->channel;
                    auto itc = std::find(g_chanTabs.begin(), g_chanTabs.end(), ch);
                    if (itc != g_chanTabs.end()) {
                        jumpTo = (uint8_t)(g_chanTabFirst + (itc - g_chanTabs.begin()));
                    }
                }
            }

            // === RESET SCROLL BEFORE JUMP ===
            // If we're going to jump or already in correct chat, reset scroll to end
            uint8_t currentFrame = ui->getUiState()->currentFrame;
            bool shouldResetScroll = false;
            
            if (isDirect) {
                // Check if we're going to jump to this DM or already in it
                if (jumpTo != 0xFF) {
                    shouldResetScroll = true; // We're going to jump to this DM
                } else if (g_favChatFirst != (size_t)-1 && currentFrame >= g_favChatFirst && currentFrame <= g_favChatLast) {
                    auto it = std::find(g_favChatNodes.begin(), g_favChatNodes.end(), packet->from);
                    if (it != g_favChatNodes.end()) {
                        uint8_t expectedFrame = (uint8_t)(g_favChatFirst + (it - g_favChatNodes.begin()));
                        if (currentFrame == expectedFrame) {
                            shouldResetScroll = true; // Ya estamos en este DM
                        }
                    }
                }
                
                if (shouldResetScroll) {
                    GlobalScrollState &st = g_nodeScroll[packet->from];
                    const auto& dmHistory = chat::ChatHistoryStore::instance().getDM(packet->from);
                    int totalMessages = (int)dmHistory.size();
                    const int maxVisibleLines = std::max(3, (dispdev->getHeight() - 20) / 10); // Minimum 3 lines
                    st.scrollIndex = std::max(0, totalMessages - maxVisibleLines);
                    st.sel = std::max(0, std::min(totalMessages - 1, maxVisibleLines - 1));
                    st.lastMs = millis(); // Marcar como actualizado
                }
            } else {
                // Mensaje de canal
                uint8_t ch = (uint8_t)packet->channel;
                if (jumpTo != 0xFF) {
                    shouldResetScroll = true; // Vamos a saltar a este canal
                } else if (g_chanTabFirst != (size_t)-1 && currentFrame >= g_chanTabFirst && currentFrame <= g_chanTabLast) {
                    auto itc = std::find(g_chanTabs.begin(), g_chanTabs.end(), ch);
                    if (itc != g_chanTabs.end()) {
                        uint8_t expectedFrame = (uint8_t)(g_chanTabFirst + (itc - g_chanTabs.begin()));
                        if (currentFrame == expectedFrame) {
                            shouldResetScroll = true; // Ya estamos en este canal
                        }
                    }
                }
                
                if (shouldResetScroll) {
                    GlobalScrollState &st = g_chanScroll[ch];
                    const auto& chanHistory = chat::ChatHistoryStore::instance().getCHAN(ch);
                    int totalMessages = (int)chanHistory.size();
                    const int maxVisibleLines = std::max(3, (dispdev->getHeight() - 20) / 10); // Minimum 3 lines
                    st.scrollIndex = std::max(0, totalMessages - maxVisibleLines);
                    st.sel = std::max(0, std::min(totalMessages - 1, maxVisibleLines - 1));
                    st.lastMs = millis(); // Marcar como actualizado
                }
            }

            if (jumpTo != 0xFF) {
                ui->switchToFrame(jumpTo);
                setFastFramerate();
                forceDisplay();
            }
            
            // Si reseteamos scroll, forzar redibujado adicional
            if (shouldResetScroll) {
                setFastFramerate();
                forceDisplay(true); // Force UI update
            }
        }
    }

    return 0;
}

// Triggered by MeshModules
int Screen::handleUIFrameEvent(const UIFrameEvent *event)
{
    // Block UI frame events when virtual keyboard is active
    if (NotificationRenderer::current_notification_type == notificationTypeEnum::text_input) {
        return 0;
    }

    if (showingNormalScreen) {
        // Regenerate the frameset, potentially honoring a module's internal requestFocus() call
        if (event->action == UIFrameEvent::Action::REGENERATE_FRAMESET)
            setFrames(FOCUS_MODULE);

        // Regenerate the frameset, while Attempt to maintain focus on the current frame
        else if (event->action == UIFrameEvent::Action::REGENERATE_FRAMESET_BACKGROUND)
            setFrames(FOCUS_PRESERVE);

        // Don't regenerate the frameset, just re-draw whatever is on screen ASAP
        else if (event->action == UIFrameEvent::Action::REDRAW_ONLY)
            setFastFramerate();
    }

    return 0;
}


static inline bool isLongPressEvent(int ev) {
    switch (ev) {
        case INPUT_BROKER_SELECT_LONG:
            return true;
#ifdef INPUT_BROKER_USER_LONG
        case INPUT_BROKER_USER_LONG:
            return true;
#endif
#ifdef INPUT_BROKER_ALT_PRESS_LONG
        case INPUT_BROKER_ALT_PRESS_LONG:
            return true;
#endif
#ifdef INPUT_BROKER_USER_HOLD
        case INPUT_BROKER_USER_HOLD:
            return true;
#endif
#ifdef INPUT_BROKER_LONG_PRESS
        case INPUT_BROKER_LONG_PRESS:
            return true;
#endif
        default:
            return false;
    }
}


int Screen::handleInputEvent(const InputEvent *event)
{
    LOG_DEBUG("=== INPUT EVENT === event=%d, kbchar=%d, showingNormal=%d, favNode=%d",
              event->inputEvent, event->kbchar, showingNormalScreen,
              graphics::UIRenderer::currentFavoriteNodeNum);

    // Update interaction timestamp for marquee timeout
    updateLastInteraction();

    if (!screenOn)
        return 0;

    // Handle text input notifications specially - pass input to virtual keyboard
    if (NotificationRenderer::current_notification_type == notificationTypeEnum::text_input) {
        NotificationRenderer::inEvent = *event;
        static OverlayCallback overlays[] = {graphics::UIRenderer::drawNavigationBar, NotificationRenderer::drawBannercallback};
        ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));
        setFastFramerate(); // Draw ASAP
        ui->update();
        return 0;
    }

#ifdef USE_EINK // the screen is the last input handler, so if an event makes it here, we can assume it will prompt a screen draw.
    EINK_ADD_FRAMEFLAG(dispdev, DEMAND_FAST); // Use fast-refresh for next frame, no skip please
    EINK_ADD_FRAMEFLAG(dispdev, BLOCKING);    // Edge case: if this frame is promoted to COSMETIC, wait for update
    handleSetOn(true);                        // Ensure power-on to receive deep-sleep screensaver (PowerFSM should handle?)
    setFastFramerate();                       // Draw ASAP
#endif
    if (NotificationRenderer::isOverlayBannerShowing()) {
        NotificationRenderer::inEvent = *event;
        static OverlayCallback overlays[] = {graphics::UIRenderer::drawNavigationBar, NotificationRenderer::drawBannercallback};
        ui->setOverlays(overlays, sizeof(overlays) / sizeof(overlays[0]));
        setFastFramerate(); // Draw ASAP
        ui->update();

        menuHandler::handleMenuSwitch(dispdev);
        return 0;
    }

    // === DEBUG: NodeInfo Input Handling ===
    if (graphics::UIRenderer::currentFavoriteNodeNum != 0) {
        LOG_DEBUG("NodeInfo input - showingNormal=%d, favNode=%d, event=%d, kbchar=%d", 
                  showingNormalScreen, graphics::UIRenderer::currentFavoriteNodeNum, 
                  event->inputEvent, event->kbchar);
        
        // ANY key should close NodeInfo and return to normal frames
        graphics::UIRenderer::currentFavoriteNodeNum = 0;
        setFrames(FOCUS_PRESERVE);
        LOG_DEBUG("NodeInfo closed, returning to normal frames");
        return 1; // Consumed
    }

    // === MQTT Status Input Handling ===
#if HAS_WIFI && !defined(ARCH_PORTDUINO)
    if (graphics::UIRenderer::showingMqttStatus) {
        LOG_DEBUG("MQTT Status input - showingNormal=%d, event=%d, kbchar=%d", 
                  showingNormalScreen, event->inputEvent, event->kbchar);
        
        // ANY key should close MQTT status and return to normal frames
        graphics::UIRenderer::showingMqttStatus = false;
        setFrames(FOCUS_PRESERVE);
        LOG_DEBUG("MQTT Status closed, returning to normal frames");
        return 1; // Consumed
    }
#endif

    // Use left or right input from a keyboard to move between frames,
    // so long as a mesh module isn't using these events for some other purpose
    if (showingNormalScreen) {

        // Ask any MeshModules if they're handling keyboard input right now
        bool inputIntercepted = false;
        for (MeshModule *module : moduleFrames) {
            if (module && module->interceptingKeyboardInput())
                inputIntercepted = true;
        }

        // If no modules are using the input, move between frames
        if (!inputIntercepted) {
            // === Are we in a chat tab? ===
            uint8_t cf = this->ui->getUiState()->currentFrame;
            bool inNodeChat = (g_favChatFirst != (size_t)-1 && cf >= g_favChatFirst && cf <= g_favChatLast);
            bool inChanChat = (g_chanTabFirst != (size_t)-1 && cf >= g_chanTabFirst && cf <= g_chanTabLast);

            // Helper function to calculate how many messages fit on screen with dynamic heights
            auto calculateVisibleRowsDM = [&](uint32_t nodeId, int scrollIndex) -> int {
                const auto &q = chat::ChatHistoryStore::instance().getDM(nodeId);
                const int h = dispdev->getHeight();
                const int lineH = 10;
                const int availableHeight = h - 16; // account for UI elements
                const int total = (int)q.size();
                
                int usedHeight = 0;
                int visibleCount = 0;
                
                for (int i = 0; i < total - scrollIndex; ++i) {
                    int itemIndex = total - 1 - (scrollIndex + i);
                    if (itemIndex < 0) break;
                    
                    const auto &e = q[itemIndex];
                    std::string who = e.outgoing ? "S" : "R";
                    std::string base = who + ": " + e.text;
                    bool needsExtra = needsExtraHeight(base);
                    int currentLineH = needsExtra ? lineH * 3 : lineH;
                    
                    if (usedHeight + currentLineH <= availableHeight) {
                        usedHeight += currentLineH;
                        visibleCount++;
                    } else {
                        break;
                    }
                }
                
                return std::max(1, std::min(visibleCount, 4)); // Ensure at least 1, max 4
            };

            auto moveSelDM = [&](uint32_t nodeId, int dir) {
                const auto &q = chat::ChatHistoryStore::instance().getDM(nodeId);
                const int total = (int)q.size();
                if (total <= 0) return;
                
                GlobalScrollState &st = g_nodeScroll[nodeId];
                const int visibleRows = calculateVisibleRowsDM(nodeId, st.scrollIndex);
                
                // Sliding window navigation
                if (dir > 0) {
                    if (st.sel < visibleRows - 1) {
                        st.sel++;
                    } else if (st.scrollIndex < total - visibleRows) {
                        st.scrollIndex++;
                        // Recalculate visible rows after scroll
                        int newVisibleRows = calculateVisibleRowsDM(nodeId, st.scrollIndex);
                        if (st.sel >= newVisibleRows) {
                            st.sel = newVisibleRows - 1;
                        }
                    } else {
                        // wrap to top
                        st.sel = 0;
                        st.scrollIndex = 0;
                    }
                } else if (dir < 0) {
                    if (st.sel > 0) {
                        st.sel--;
                    } else if (st.scrollIndex > 0) {
                        st.scrollIndex--;
                        // Recalculate visible rows after scroll
                        int newVisibleRows = calculateVisibleRowsDM(nodeId, st.scrollIndex);
                        if (st.sel >= newVisibleRows) {
                            st.sel = newVisibleRows - 1;
                        }
                    } else {
                        // wrap to bottom
                        st.scrollIndex = total - visibleRows;
                        st.sel = visibleRows - 1;
                    }
                }
                st.offset = 0; st.lastMs = millis();
                setFastFramerate(); forceDisplay();
            };
            // Helper function to calculate how many messages fit on screen with dynamic heights for channels
            auto calculateVisibleRowsCH = [&](uint8_t ch, int scrollIndex) -> int {
                const auto &q = chat::ChatHistoryStore::instance().getCHAN(ch);
                const int h = dispdev->getHeight();
                const int lineH = 10;
                const int availableHeight = h - 16; // account for UI elements
                const int total = (int)q.size();
                
                int usedHeight = 0;
                int visibleCount = 0;
                
                for (int i = 0; i < total - scrollIndex; ++i) {
                    int itemIndex = total - 1 - (scrollIndex + i);
                    if (itemIndex < 0) break;
                    
                    const auto &e = q[itemIndex];
                    std::string who = e.outgoing ? "S" : "R";
                    std::string base = who + ": " + e.text;
                    bool needsExtra = needsExtraHeight(base);
                    int currentLineH = needsExtra ? lineH * 3 : lineH;
                    
                    if (usedHeight + currentLineH <= availableHeight) {
                        usedHeight += currentLineH;
                        visibleCount++;
                    } else {
                        break;
                    }
                }
                
                return std::max(1, std::min(visibleCount, 4)); // Ensure at least 1, max 4
            };

            auto moveSelCH = [&](uint8_t ch, int dir) {
                const auto &q = chat::ChatHistoryStore::instance().getCHAN(ch);
                const int total = (int)q.size();
                if (total <= 0) return;
                
                GlobalScrollState &st = g_chanScroll[ch];
                const int visibleRows = calculateVisibleRowsCH(ch, st.scrollIndex);
                
                // Sliding window navigation
                if (dir > 0) {
                    if (st.sel < visibleRows - 1) {
                        st.sel++;
                    } else if (st.scrollIndex < total - visibleRows) {
                        st.scrollIndex++;
                        // Recalculate visible rows after scroll
                        int newVisibleRows = calculateVisibleRowsCH(ch, st.scrollIndex);
                        if (st.sel >= newVisibleRows) {
                            st.sel = newVisibleRows - 1;
                        }
                    } else {
                        // wrap to top
                        st.sel = 0;
                        st.scrollIndex = 0;
                    }
                } else if (dir < 0) {
                    if (st.sel > 0) {
                        st.sel--;
                    } else if (st.scrollIndex > 0) {
                        st.scrollIndex--;
                        // Recalculate visible rows after scroll
                        int newVisibleRows = calculateVisibleRowsCH(ch, st.scrollIndex);
                        if (st.sel >= newVisibleRows) {
                            st.sel = newVisibleRows - 1;
                        }
                    } else {
                        // wrap to bottom
                        st.scrollIndex = total - visibleRows;
                        st.sel = visibleRows - 1;
                    }
                }
                st.offset = 0; st.lastMs = millis();
                setFastFramerate(); forceDisplay();
            };

            // --- Scroll by short press in the CHAT SCREEN ---
            const bool shortPressAsDown =
                g_chatScrollByPress &&
                (inNodeChat || inChanChat) &&
                (event->inputEvent == INPUT_BROKER_USER_PRESS || event->inputEvent == INPUT_BROKER_SELECT);

            if (inNodeChat || inChanChat) {
                // --- move selection with UP/DOWN ---
                if (event->inputEvent == INPUT_BROKER_UP) {
                    if (inNodeChat) {
                        uint32_t nodeId = g_favChatNodes[(size_t)cf - g_favChatFirst];
                        moveSelDM(nodeId, -1);
                    } else {
                        uint8_t ch = g_chanTabs[(size_t)cf - g_chanTabFirst];
                        moveSelCH(ch, -1);
                    }
                    return 1;
                }

                if (event->inputEvent == INPUT_BROKER_DOWN) {
                    if (inNodeChat) {
                        uint32_t nodeId = g_favChatNodes[(size_t)cf - g_favChatFirst];
                        moveSelDM(nodeId, +1);
                    } else {
                        uint8_t ch = g_chanTabs[(size_t)cf - g_chanTabFirst];
                        moveSelCH(ch, +1);
                    }
                    return 1;
                }

                // --- scroll by SHORT PRESS (only if enabled) ---
                if (g_chatScrollByPress && event->inputEvent == INPUT_BROKER_USER_PRESS) {
                    int direction = g_chatScrollUpDown ? +1 : -1;  // UP = +1, DOWN = -1
                    if (inNodeChat) {
                        uint32_t nodeId = g_favChatNodes[(size_t)cf - g_favChatFirst];
                        moveSelDM(nodeId, direction);
                    } else {
                        uint8_t ch = g_chanTabs[(size_t)cf - g_chanTabFirst];
                        moveSelCH(ch, direction);
                    }
                    return 1;
                }

                // --- open chat menu with SELECT or SELECT_LONG (always) ---
                if (event->inputEvent == INPUT_BROKER_SELECT ||
                    event->inputEvent == INPUT_BROKER_SELECT_LONG) {
                    if (inNodeChat) {
                        size_t idx = (size_t)cf - g_favChatFirst;
                        if (idx < g_favChatNodes.size()) graphics::menuHandler::openChatActionsForNode(g_favChatNodes[idx]);
                    } else {
                        size_t idx = (size_t)cf - g_chanTabFirst;
                        if (idx < g_chanTabs.size()) graphics::menuHandler::openChatActionsForChannel(g_chanTabs[idx]);
                    }
                    return 1;
                }
            }

            // === GLOBAL BEHAVIOR: UP/DOWN = navigate frames ===
            if (event->inputEvent == INPUT_BROKER_UP) {
                showPrevFrame();
                return 1;
            } else if (event->inputEvent == INPUT_BROKER_DOWN) {
                showNextFrame();
                return 1;
            }

            // === Original global navigation ===
            if (event->inputEvent == INPUT_BROKER_LEFT || event->inputEvent == INPUT_BROKER_ALT_PRESS) {
                showPrevFrame();
            } else if (event->inputEvent == INPUT_BROKER_RIGHT || event->inputEvent == INPUT_BROKER_USER_PRESS) {
                showNextFrame();
            } else if (event->inputEvent == INPUT_BROKER_SELECT) {
                uint8_t cff = this->ui->getUiState()->currentFrame;

                if (cff == framesetInfo.positions.home) {
                    menuHandler::homeBaseMenu();
                } else if (cff == framesetInfo.positions.system) {
                    menuHandler::systemBaseMenu();
#if HAS_GPS
                } else if (cff == framesetInfo.positions.gps && gps) {
                    menuHandler::positionBaseMenu();
#endif
                } else if (cff == framesetInfo.positions.clock) {
                    menuHandler::clockMenu();
                } else if (cff == framesetInfo.positions.lora) {
                    menuHandler::loraMenu();
                } else if (cff == framesetInfo.positions.textMessage) {
                    if (devicestate.rx_text_message.from) {
                        menuHandler::messageResponseMenu();
                    } else {
#if defined(M5STACK_UNITC6L)
                        menuHandler::textMessageMenu();
#else
                        menuHandler::textMessageBaseMenu();
#endif
                    }
                } else if (framesetInfo.positions.firstFavorite != 255 &&
                           cff >= framesetInfo.positions.firstFavorite &&
                           cff <= framesetInfo.positions.lastFavorite) {
                    menuHandler::favoriteBaseMenu();
                } else if (cff == framesetInfo.positions.nodelist ||
                           cff == framesetInfo.positions.nodelist_lastheard ||
                           cff == framesetInfo.positions.nodelist_hopsignal ||
                           cff == framesetInfo.positions.nodelist_distance ||
                           cff == framesetInfo.positions.nodelist_hopsignal ||
                           cff == framesetInfo.positions.nodelist_bearings) {
                    menuHandler::nodeListMenu();
                } else if (cff == framesetInfo.positions.wifi) {
                    menuHandler::wifiBaseMenu();
                }
            } else if (event->inputEvent == INPUT_BROKER_BACK) {
                showPrevFrame();
            } else if (event->inputEvent == INPUT_BROKER_CANCEL) {
                setOn(false);
            }
        }
    }

    return 0;
}

int Screen::handleAdminMessage(AdminModule_ObserverData *arg)
{
    switch (arg->request->which_payload_variant) {
    // Node removed manually (i.e. via app)
    case meshtastic_AdminMessage_remove_by_nodenum_tag:
        setFrames(FOCUS_PRESERVE);
        *arg->result = AdminMessageHandleResult::HANDLED;
        break;

    // Default no-op, in case the admin message observable gets used by other classes in future
    default:
        break;
    }
    return 0;
}

bool Screen::isOverlayBannerShowing()
{
    return NotificationRenderer::isOverlayBannerShowing();
}

} // namespace graphics

#else
graphics::Screen::Screen(ScanI2C::DeviceAddress, meshtastic_Config_DisplayConfig_OledType, OLEDDISPLAY_GEOMETRY) {}
#endif // HAS_SCREEN

bool shouldWakeOnReceivedMessage()
{
    /*
    The goal here is to determine when we do NOT wake up the screen on message received:
    - Chat silent mode is enabled
    - Any ext. notifications are turned on
    - If role is not CLIENT / CLIENT_MUTE / CLIENT_HIDDEN / CLIENT_BASE
    - If the battery level is very low
    */
    
    // Check silent mode first
    if (g_chatSilentMode) {
        return false;
    }
    
    if (moduleConfig.external_notification.enabled) {
        return false;
    }
    if (!IS_ONE_OF(config.device.role, meshtastic_Config_DeviceConfig_Role_CLIENT,
                   meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE, meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN,
                   meshtastic_Config_DeviceConfig_Role_CLIENT_BASE)) {
        return false;
    }
    if (powerStatus && powerStatus->getBatteryChargePercent() < 10) {
        return false;
    }
    return true;
}
