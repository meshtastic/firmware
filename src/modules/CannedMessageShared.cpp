#include "configuration.h"
#if HAS_SCREEN
#include "CannedMessageModule.h"
#include "Channels.h"
#include "MeshService.h"
#include "MessageStore.h"
#include "NodeDB.h"
#include "buzz.h"
#include "gps/RTC.h"
#include "graphics/Screen.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/draw/MessageRenderer.h"
#include "graphics/draw/NotificationRenderer.h"
#include "graphics/emotes.h"
#include "modules/FreeTextModule.h"
#include <algorithm>
#include <cstring>

extern MessageStore messageStore;
extern bool osk_found;
extern NodeNum lastDest;
extern uint8_t lastChannel;
extern bool lastDestSet;
namespace graphics
{
extern int bannerSignalBars;
}

// Tracks whether destination-picker cancel/select should return to canned list vs freetext.
static bool returnToCannedList = false;

// Reset destination search state and keep the previous selection roughly centered.
void CannedMessageModule::resetSearch()
{
    int previousDestIndex = destIndex;

    searchQuery = "";
    updateDestinationSelectionList();

    // Adjust scrollIndex so previousDestIndex is still visible
    int totalEntries = activeChannelIndices.size() + filteredNodes.size();
    this->visibleRows = (displayHeight - FONT_HEIGHT_SMALL * 2) / FONT_HEIGHT_SMALL;
    if (this->visibleRows < 1)
        this->visibleRows = 1;
    int maxScrollIndex = std::max(0, totalEntries - visibleRows);
    scrollIndex = std::min(std::max(previousDestIndex - (visibleRows / 2), 0), maxScrollIndex);

    lastUpdateMillis = millis();
    requestFocus();
}

// Rebuild searchable destination entries (channels + eligible nodes).
void CannedMessageModule::updateDestinationSelectionList()
{
    static size_t lastNumMeshNodes = 0;
    static String lastSearchQuery = "";

    size_t numMeshNodes = nodeDB->getNumMeshNodes();
    bool nodesChanged = (numMeshNodes != lastNumMeshNodes);
    lastNumMeshNodes = numMeshNodes;

    // Early exit if nothing changed
    if (searchQuery == lastSearchQuery && !nodesChanged)
        return;
    lastSearchQuery = searchQuery;
    needsUpdate = false;

    this->filteredNodes.clear();
    this->activeChannelIndices.clear();

    NodeNum myNodeNum = nodeDB->getNodeNum();
    String lowerSearchQuery = searchQuery;
    lowerSearchQuery.toLowerCase();

    // Preallocate space to reduce reallocation
    this->filteredNodes.reserve(numMeshNodes);

    for (size_t i = 0; i < numMeshNodes; ++i) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
        if (!node || node->num == myNodeNum || !node->has_user || node->user.public_key.size != 32)
            continue;

        const String &nodeName = node->user.long_name;

        if (searchQuery.length() == 0) {
            this->filteredNodes.push_back({node, sinceLastSeen(node)});
        } else {
            // Avoid unnecessary lowercase conversion if already matched
            String lowerNodeName = nodeName;
            lowerNodeName.toLowerCase();

            if (lowerNodeName.indexOf(lowerSearchQuery) != -1) {
                this->filteredNodes.push_back({node, sinceLastSeen(node)});
            }
        }
    }

    meshtastic_MeshPacket *p = allocDataPacket();
    p->pki_encrypted = true;
    p->channel = 0;

    // Populate active channels
    std::vector<String> seenChannels;
    seenChannels.reserve(channels.getNumChannels());
    for (uint8_t i = 0; i < channels.getNumChannels(); ++i) {
        String name = channels.getName(i);
        if (name.length() > 0 && std::find(seenChannels.begin(), seenChannels.end(), name) == seenChannels.end()) {
            this->activeChannelIndices.push_back(i);
            seenChannels.push_back(name);
        }
    }

    scrollIndex = 0; // Show first result at the top
    destIndex = 0;   // Highlight the first entry
    if (nodesChanged && runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION) {
        LOG_INFO("Nodes changed, forcing UI refresh.");
        screen->forceDisplay();
    }
}

// Handle destination picker input: type-to-filter, navigation, select, and cancel.
int CannedMessageModule::handleDestinationSelectionInput(const InputEvent *event, bool isUp, bool isDown, bool isSelect)
{
    // Override isDown and isSelect ONLY for destination selector behavior
    if (runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION) {
        if (event->inputEvent == INPUT_BROKER_USER_PRESS) {
            isDown = true;
        } else if (event->inputEvent == INPUT_BROKER_SELECT) {
            isSelect = true;
        }
    }

    if (event->kbchar >= 32 && event->kbchar <= 126 && !isUp && !isDown && event->inputEvent != INPUT_BROKER_LEFT &&
        event->inputEvent != INPUT_BROKER_RIGHT && event->inputEvent != INPUT_BROKER_SELECT) {
        this->searchQuery += (char)event->kbchar;
        needsUpdate = true;
        if ((millis() - lastFilterUpdate) > filterDebounceMs) {
            runOnce(); // update filter immediately
            lastFilterUpdate = millis();
        }
        return 1;
    }

    size_t numMeshNodes = filteredNodes.size();
    int totalEntries = numMeshNodes + activeChannelIndices.size();
    int columns = 1;
    int totalRows = totalEntries;
    int maxScrollIndex = std::max(0, totalRows - visibleRows);
    scrollIndex = clamp(scrollIndex, 0, maxScrollIndex);

    // Handle backspace
    if (event->inputEvent == INPUT_BROKER_BACK) {
        if (searchQuery.length() > 0) {
            searchQuery.remove(searchQuery.length() - 1);
            needsUpdate = true;
            runOnce();
        }
        if (searchQuery.length() == 0) {
            resetSearch();
            needsUpdate = false;
        }
        return 1;
    }

    if (isUp) {
        if (destIndex > 0) {
            destIndex--;
        } else if (totalEntries > 0) {
            destIndex = totalEntries - 1;
        }

        if ((destIndex / columns) < scrollIndex)
            scrollIndex = destIndex / columns;
        else if ((destIndex / columns) >= (scrollIndex + visibleRows))
            scrollIndex = (destIndex / columns) - visibleRows + 1;

        screen->forceDisplay(true);
        return 1;
    }

    if (isDown) {
        if (destIndex + 1 < totalEntries) {
            destIndex++;
        } else if (totalEntries > 0) {
            destIndex = 0;
            scrollIndex = 0;
        }

        if ((destIndex / columns) >= (scrollIndex + visibleRows))
            scrollIndex = (destIndex / columns) - visibleRows + 1;

        screen->forceDisplay(true);
        return 1;
    }

    // SELECT
    if (isSelect) {
        if (destIndex < static_cast<int>(activeChannelIndices.size())) {
            dest = NODENUM_BROADCAST;
            channel = activeChannelIndices[destIndex];
            lastDest = dest;
            lastChannel = channel;
            lastDestSet = true;
        } else {
            int nodeIndex = destIndex - static_cast<int>(activeChannelIndices.size());
            if (nodeIndex >= 0 && nodeIndex < static_cast<int>(filteredNodes.size())) {
                const meshtastic_NodeInfoLite *selectedNode = filteredNodes[nodeIndex].node;
                if (selectedNode) {
                    dest = selectedNode->num;
                    channel = selectedNode->channel;
                    // Already saves here, but for clarity, also:
                    lastDest = dest;
                    lastChannel = channel;
                    lastDestSet = true;
                }
            }
        }

        runState = returnToCannedList ? CANNED_MESSAGE_RUN_STATE_ACTIVE : CANNED_MESSAGE_RUN_STATE_FREETEXT;
        returnToCannedList = false;
        screen->forceDisplay(true);
        return 1;
    }

    // CANCEL
    if (event->inputEvent == INPUT_BROKER_CANCEL || event->inputEvent == INPUT_BROKER_ALT_LONG) {
        runState = returnToCannedList ? CANNED_MESSAGE_RUN_STATE_ACTIVE : CANNED_MESSAGE_RUN_STATE_FREETEXT;
        returnToCannedList = false;
        searchQuery = "";

        // UIFrameEvent e;
        // e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        // notifyObservers(&e);
        screen->forceDisplay(true);
        return 1;
    }

    return 0;
}

// Handle canned-message list input including destination/freetext/exit actions.
bool CannedMessageModule::handleMessageSelectorInput(const InputEvent *event, bool isUp, bool isDown, bool isSelect)
{
    // Override isDown and isSelect ONLY for canned message list behavior
    if (runState == CANNED_MESSAGE_RUN_STATE_ACTIVE) {
        if (event->inputEvent == INPUT_BROKER_USER_PRESS) {
            isDown = true;
        } else if (event->inputEvent == INPUT_BROKER_SELECT) {
            isSelect = true;
        }
    }

    if (runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION)
        return false;

    // Handle Cancel key: go inactive, clear UI state
    if (runState != CANNED_MESSAGE_RUN_STATE_INACTIVE &&
        (event->inputEvent == INPUT_BROKER_CANCEL || event->inputEvent == INPUT_BROKER_ALT_LONG)) {
        runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
        freetext = "";
        cursor = 0;
        payload = 0;
        currentMessageIndex = -1;

        // Notify UI that we want to redraw/close this screen
        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
        screen->forceDisplay();
        return true;
    }

    bool handled = false;

    // Handle up/down navigation
    if (isUp && messagesCount > 0) {
        runState = CANNED_MESSAGE_RUN_STATE_ACTION_UP;
        handled = true;
    } else if (isDown && messagesCount > 0) {
        runState = CANNED_MESSAGE_RUN_STATE_ACTION_DOWN;
        handled = true;
    } else if (isSelect) {
        const char *current = messages[currentMessageIndex];

        // [Select Destination] triggers destination selection UI
        if (strcmp(current, "[Select Destination]") == 0) {
            returnToCannedList = true;
            runState = CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION;
            destIndex = 0;
            scrollIndex = 0;
            updateDestinationSelectionList(); // Make sure list is fresh
            screen->forceDisplay();
            return true;
        }

        // [Exit] returns to the main/inactive screen
        if (strcmp(current, "[Exit]") == 0) {
            // Set runState to inactive so we return to main UI
            runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
            currentMessageIndex = -1;

            // Notify UI to regenerate frame set and redraw
            UIFrameEvent e;
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            notifyObservers(&e);
            screen->forceDisplay();
            return true;
        }

        // [Free Text] triggers the free text input (virtual keyboard)
#if defined(USE_VIRTUAL_KEYBOARD)
        if (strcmp(current, "[-- Free Text --]") == 0) {
            runState = CANNED_MESSAGE_RUN_STATE_FREETEXT;
            requestFocus();
            UIFrameEvent e;
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            notifyObservers(&e);
            return true;
        }
#else
        if (strcmp(current, "[-- Free Text --]") == 0) {
            if (osk_found && screen) {
                char headerBuffer[64];
                if (this->dest == NODENUM_BROADCAST) {
                    snprintf(headerBuffer, sizeof(headerBuffer), "To: #%s", channels.getName(this->channel));
                } else {
                    snprintf(headerBuffer, sizeof(headerBuffer), "To: @%s", getNodeName(this->dest));
                }
                screen->showTextInput(headerBuffer, "", 300000, [this](const std::string &text) {
                    if (!text.empty()) {
                        this->freetext = text.c_str();
                        this->payload = CANNED_MESSAGE_RUN_STATE_FREETEXT;
                        runState = CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE;
                        currentMessageIndex = -1;

                        UIFrameEvent e;
                        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
                        this->notifyObservers(&e);
                        screen->forceDisplay();

                        setIntervalFromNow(500);
                        return;
                    } else {
                        // Don't delete virtual keyboard immediately - it might still be executing
                        // Instead, just clear the callback and reset banner to stop input processing
                        graphics::NotificationRenderer::textInputCallback = nullptr;
                        graphics::NotificationRenderer::resetBanner();

                        // Return to inactive state
                        this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
                        this->currentMessageIndex = -1;
                        this->freetext = "";
                        this->cursor = 0;

                        // Force display update to show normal screen
                        UIFrameEvent e;
                        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
                        this->notifyObservers(&e);
                        screen->forceDisplay();

                        // Schedule cleanup for next loop iteration to ensure safe deletion
                        setIntervalFromNow(50);
                        return;
                    }
                });

                return true;
            }
        }
#endif

        // Normal canned message selection
        if (runState == CANNED_MESSAGE_RUN_STATE_INACTIVE || runState == CANNED_MESSAGE_RUN_STATE_DISABLED) {
        } else {
#if CANNED_MESSAGE_ADD_CONFIRMATION
            const int savedIndex = currentMessageIndex;
            graphics::menuHandler::showConfirmationBanner("Send message?", [this, savedIndex]() {
                this->currentMessageIndex = savedIndex;
                this->payload = this->runState;
                this->runState = CANNED_MESSAGE_RUN_STATE_ACTION_SELECT;
                this->setIntervalFromNow(0);
            });
#else
            payload = runState;
            runState = CANNED_MESSAGE_RUN_STATE_ACTION_SELECT;
#endif
            // Do not immediately set runState; wait for confirmation
            handled = true;
        }
    }

    if (handled) {
        requestFocus();
        if (runState == CANNED_MESSAGE_RUN_STATE_ACTION_SELECT)
            setIntervalFromNow(0);
        else
            runOnce();
    }

    return handled;
}

// Build and send a text packet, persist it locally, and switch to text-message view.
void CannedMessageModule::sendText(NodeNum dest, ChannelIndex channel, const char *message, bool wantReplies)
{
    lastDest = dest;
    lastChannel = channel;
    lastDestSet = true;

    meshtastic_MeshPacket *p = allocDataPacket();
    p->to = dest;
    p->channel = channel;
    p->want_ack = true;
    p->decoded.dest = dest; // Mirror picker: NODENUM_BROADCAST or node->num

    this->lastSentNode = dest;
    this->incoming = dest;

    // Manually find the node by number to check PKI capability
    meshtastic_NodeInfoLite *node = nullptr;
    size_t numMeshNodes = nodeDB->getNumMeshNodes();
    for (size_t i = 0; i < numMeshNodes; ++i) {
        meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(i);
        if (n && n->num == dest) {
            node = n;
            break;
        }
    }

    NodeNum myNodeNum = nodeDB->getNodeNum();
    if (node && node->num != myNodeNum && node->has_user && node->user.public_key.size == 32) {
        p->pki_encrypted = true;
        p->channel = 0; // force PKI
    }

    // Track this packet’s request ID for matching ACKs
    this->lastRequestId = p->id;

    // Copy payload
    p->decoded.payload.size = strlen(message);
    memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);

    if (moduleConfig.canned_message.send_bell && p->decoded.payload.size < meshtastic_Constants_DATA_PAYLOAD_LEN) {
        p->decoded.payload.bytes[p->decoded.payload.size++] = 7;
        p->decoded.payload.bytes[p->decoded.payload.size] = '\0';
    }

    this->waitingForAck = true;

    // Send to mesh (PKI-encrypted if conditions above matched)
    service->sendToMesh(p, RX_SRC_LOCAL, true);

    // Show banner immediately
    if (screen) {
        graphics::BannerOverlayOptions opts;
        opts.message = "Sending...";
        opts.durationMs = 2000;
        screen->showOverlayBanner(opts);
    }

    // Save outgoing message
    StoredMessage sm;

    // Always use our local time, consistent with other paths
    uint32_t nowSecs = getValidTime(RTCQuality::RTCQualityDevice, true);
    sm.timestamp = (nowSecs > 0) ? nowSecs : millis() / 1000;
    sm.isBootRelative = (nowSecs == 0);

    sm.sender = nodeDB->getNodeNum(); // us
    sm.channelIndex = channel;
    size_t len = strnlen(message, MAX_MESSAGE_SIZE - 1);
    sm.textOffset = MessageStore::storeText(message, len);
    sm.textLength = len;

    // Classify broadcast vs DM
    if (dest == NODENUM_BROADCAST) {
        sm.dest = NODENUM_BROADCAST;
        sm.type = MessageType::BROADCAST;
    } else {
        sm.dest = dest;
        sm.type = MessageType::DM_TO_US;
        // Only add as favorite if our role is NOT CLIENT_BASE
        if (config.device.role != 12) {
            LOG_INFO("Proactively adding %x as favorite node", dest);
            nodeDB->set_favorite(true, dest);
        } else {
            LOG_DEBUG("Not favoriting node %x as we are CLIENT_BASE role", dest);
        }
    }
    sm.ackStatus = AckStatus::NONE;

    messageStore.addLiveMessage(std::move(sm));

    // Auto-switch thread view on outgoing message
    if (sm.type == MessageType::BROADCAST) {
        graphics::MessageRenderer::setThreadMode(graphics::MessageRenderer::ThreadMode::CHANNEL, sm.channelIndex);
    } else {
        graphics::MessageRenderer::setThreadMode(graphics::MessageRenderer::ThreadMode::DIRECT, -1, sm.dest);
    }

    playComboTune();

    this->runState = CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE;
    this->payload = wantReplies ? 1 : 0;
    requestFocus();

    // Tell Screen to switch to TextMessage frame via UIFrameEvent
    UIFrameEvent e;
    e.action = UIFrameEvent::Action::SWITCH_TO_TEXTMESSAGE;
    notifyObservers(&e);
}

// Draw the destination picker list with highlight, truncation, and scrollbar.
void CannedMessageModule::drawDestinationSelectionScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    requestFocus();
    display->setColor(WHITE); // Always draw cleanly
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    // Header
    int titleY = 2;
    String titleText = "Select Destination";
    titleText += searchQuery.length() > 0 ? " [" + searchQuery + "]" : " [ ]";
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(display->getWidth() / 2, titleY, titleText);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    // List Items
    int rowYOffset = titleY + (FONT_HEIGHT_SMALL - 4);
    int numActiveChannels = this->activeChannelIndices.size();
    int totalEntries = numActiveChannels + this->filteredNodes.size();
    int columns = 1;
    this->visibleRows = (display->getHeight() - (titleY + FONT_HEIGHT_SMALL)) / (FONT_HEIGHT_SMALL - 4);
    if (this->visibleRows < 1)
        this->visibleRows = 1;

    // Clamp scrolling
    if (scrollIndex > totalEntries / columns)
        scrollIndex = totalEntries / columns;
    if (scrollIndex < 0)
        scrollIndex = 0;

    for (int row = 0; row < visibleRows; row++) {
        int itemIndex = scrollIndex + row;
        if (itemIndex >= totalEntries)
            break;

        int xOffset = 0;
        int yOffset = row * (FONT_HEIGHT_SMALL - 4) + rowYOffset;
        char entryText[64] = "";

        // Draw Channels First
        if (itemIndex < numActiveChannels) {
            uint8_t channelIndex = this->activeChannelIndices[itemIndex];
            snprintf(entryText, sizeof(entryText), "#%s", channels.getName(channelIndex));
        }
        // Then Draw Nodes
        else {
            int nodeIndex = itemIndex - numActiveChannels;
            if (nodeIndex >= 0 && nodeIndex < static_cast<int>(this->filteredNodes.size())) {
                meshtastic_NodeInfoLite *node = this->filteredNodes[nodeIndex].node;
                if (node && node->user.long_name) {
                    strncpy(entryText, node->user.long_name, sizeof(entryText) - 1);
                    entryText[sizeof(entryText) - 1] = '\0';
                }
                int availWidth = display->getWidth() -
                                 ((graphics::currentResolution == graphics::ScreenResolution::High) ? 40 : 20) -
                                 ((node && node->is_favorite) ? 10 : 0);
                if (availWidth < 0)
                    availWidth = 0;

                size_t origLen = strlen(entryText);
                while (entryText[0] && display->getStringWidth(entryText) > availWidth) {
                    entryText[strlen(entryText) - 1] = '\0';
                }
                if (strlen(entryText) < origLen) {
                    strcat(entryText, "...");
                }

                // Prepend "* " if this is a favorite
                if (node && node->is_favorite) {
                    size_t len = strlen(entryText);
                    if (len + 2 < sizeof(entryText)) {
                        memmove(entryText + 2, entryText, len + 1);
                        entryText[0] = '*';
                        entryText[1] = ' ';
                    }
                }
                if (node) {
                    if (display->getWidth() <= 64) {
                        snprintf(entryText, sizeof(entryText), "%s", node->user.short_name);
                    }
                }
            }
        }

        if (strlen(entryText) == 0 || strcmp(entryText, "Unknown") == 0)
            strcpy(entryText, "?");

        // Highlight background (if selected)
        if (itemIndex == destIndex) {
            int scrollPadding = 8; // Reserve space for scrollbar
            display->fillRect(0, yOffset + 2, display->getWidth() - scrollPadding, FONT_HEIGHT_SMALL - 5);
            display->setColor(BLACK);
        }

        // Draw entry text
        display->drawString(xOffset + 2, yOffset, entryText);
        display->setColor(WHITE);

        // Draw key icon (after highlight)
        /*
        if (itemIndex >= numActiveChannels) {
            int nodeIndex = itemIndex - numActiveChannels;
            if (nodeIndex >= 0 && nodeIndex < static_cast<int>(this->filteredNodes.size())) {
                const meshtastic_NodeInfoLite *node = this->filteredNodes[nodeIndex].node;
                if (node && hasKeyForNode(node)) {
                    int iconX = display->getWidth() - key_symbol_width - 15;
                    int iconY = yOffset + (FONT_HEIGHT_SMALL - key_symbol_height) / 2;

                    if (itemIndex == destIndex) {
                        display->setColor(INVERSE);
                    } else {
                        display->setColor(WHITE);
                    }
                    display->drawXbm(iconX, iconY, key_symbol_width, key_symbol_height, key_symbol);
                }
            }
        }
        */
    }

    // Scrollbar
    if (totalEntries > visibleRows) {
        int scrollbarHeight = visibleRows * (FONT_HEIGHT_SMALL - 4);
        int totalScrollable = totalEntries;
        int scrollTrackX = display->getWidth() - 6;
        display->drawRect(scrollTrackX, rowYOffset, 4, scrollbarHeight);
        int scrollHeight = (scrollbarHeight * visibleRows) / totalScrollable;
        int scrollPos = rowYOffset + (scrollbarHeight * scrollIndex) / totalScrollable;
        display->fillRect(scrollTrackX, scrollPos, 4, scrollHeight);
    }
}

// Draw the canned messages list with selection highlight and emote-aware row layout.
void CannedMessageModule::drawCannedMessageListScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y,
                                                      char *buffer)
{
    (void)state;
    if (this->messagesCount <= 0) {
        return;
    }

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    // Precompute per-row heights based on emotes (centered if present).
    const int baseRowSpacing = FONT_HEIGHT_SMALL - 4;

    int topMsg;
    std::vector<int> rowHeights;
    int _visibleRows;

    // Draw header (To: ...).
    drawHeader(display, x, y, buffer);

    // Shift message list upward by 3 pixels to reduce spacing between header and first message.
    const int listYOffset = y + FONT_HEIGHT_SMALL - 3;
    _visibleRows = (display->getHeight() - listYOffset) / baseRowSpacing;

    // Figure out which messages are visible and their needed heights.
    topMsg =
        (messagesCount > _visibleRows && currentMessageIndex >= _visibleRows - 1) ? currentMessageIndex - _visibleRows + 2 : 0;
    int countRows = std::min(messagesCount, _visibleRows);

    // Build per-row max height based on all emotes in line.
    for (int i = 0; i < countRows; i++) {
        const char *msg = getMessageByIndex(topMsg + i);
        int maxEmoteHeight = 0;
        for (int j = 0; j < graphics::numEmotes; j++) {
            const char *label = graphics::emotes[j].label;
            if (!label || !*label)
                continue;
            const char *search = msg;
            while ((search = strstr(search, label))) {
                if (graphics::emotes[j].height > maxEmoteHeight)
                    maxEmoteHeight = graphics::emotes[j].height;
                search += strlen(label); // Advance past this emote.
            }
        }
        rowHeights.push_back(std::max(baseRowSpacing, maxEmoteHeight + 2));
    }

    // Draw all message rows with multi-emote support.
    int yCursor = listYOffset;
    for (int vis = 0; vis < countRows; vis++) {
        int msgIdx = topMsg + vis;
        int lineY = yCursor;
        const char *msg = getMessageByIndex(msgIdx);
        int rowHeight = rowHeights[vis];
        bool _highlight = (msgIdx == currentMessageIndex);

        // Multi-emote tokenization.
        std::vector<std::pair<bool, String>> tokens = freeTextModule::tokenizeMessageWithEmotes(msg);

        // Vertically center based on rowHeight.
        int textYOffset = (rowHeight - FONT_HEIGHT_SMALL) / 2;

#ifdef USE_EINK
        int nextX = x + (_highlight ? 12 : 0);
        if (_highlight)
            display->drawString(x + 0, lineY + textYOffset, ">");
#else
        int scrollPadding = 8;
        if (_highlight) {
            display->fillRect(x + 0, lineY, display->getWidth() - scrollPadding, rowHeight);
            display->setColor(BLACK);
        }
        int nextX = x + (_highlight ? 2 : 0);
#endif

        // Draw all tokens left to right.
        for (const auto &token : tokens) {
            if (token.first) {
                // Emote rendering centralized in helper.
                freeTextModule::renderEmote(display, nextX, lineY, rowHeight, token.second);
            } else {
                // Text.
                display->drawString(nextX, lineY + textYOffset, token.second);
                nextX += display->getStringWidth(token.second);
            }
        }
#ifndef USE_EINK
        if (_highlight)
            display->setColor(WHITE);
#endif

        yCursor += rowHeight;
    }

    // Scrollbar.
    if (messagesCount > _visibleRows) {
        int scrollHeight = display->getHeight() - listYOffset;
        int scrollTrackX = display->getWidth() - 6;
        display->drawRect(scrollTrackX, listYOffset, 4, scrollHeight);
        int barHeight = (scrollHeight * _visibleRows) / messagesCount;
        int scrollPos = listYOffset + (scrollHeight * topMsg) / messagesCount;
        display->fillRect(scrollTrackX, scrollPos, 4, barHeight);
    }
}

// Map modem preset to a rough SNR quality threshold used for banner grading.
static float getSnrLimit(meshtastic_Config_LoRaConfig_ModemPreset preset)
{
    switch (preset) {
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST:
        return -6.0f;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
        return -5.5f;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:
        return -4.5f;
    default:
        return -6.0f;
    }
}

// Convert RSSI/SNR into user-facing quality label and 1-5 signal bars.
static const char *getSignalGrade(float snr, int32_t rssi, float snrLimit, int &bars)
{
    // 5-bar logic: strength inside Good/Fair/Bad category
    if (snr > snrLimit && rssi > -10) {
        bars = 5; // very strong good
        return "Good";
    } else if (snr > snrLimit && rssi > -20) {
        bars = 4; // normal good
        return "Good";
    } else if (snr > 0 && rssi > -50) {
        bars = 3; // weaker good (on edge of fair)
        return "Good";
    } else if (snr > -10 && rssi > -100) {
        bars = 2; // fair
        return "Fair";
    } else {
        bars = 1; // bad
        return "Bad";
    }
}

// Process routing ACK/NACK for the most recent outbound message and show status banner.
ProcessMessage CannedMessageModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    // Only process routing ACK/NACK packets that are responses to our own outbound
    if (mp.decoded.portnum == meshtastic_PortNum_ROUTING_APP && waitingForAck && mp.to == nodeDB->getNodeNum() &&
        mp.decoded.request_id == this->lastRequestId) // only ACKs for our last sent packet
    {
        if (mp.decoded.request_id != 0) {
            // Decode the routing response
            meshtastic_Routing decoded = meshtastic_Routing_init_default;
            pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, meshtastic_Routing_fields, &decoded);

            // Determine ACK/NACK status
            bool isAck = (decoded.error_reason == meshtastic_Routing_Error_NONE);
            bool isFromDest = (mp.from == this->lastSentNode);
            bool wasBroadcast = (this->lastSentNode == NODENUM_BROADCAST);

            // Identify the responding node
            if (wasBroadcast && mp.from != nodeDB->getNodeNum()) {
                this->incoming = mp.from; // relayed by another node
            } else {
                this->incoming = this->lastSentNode; // direct reply
            }

            // Final ACK/NACK logic
            if (wasBroadcast) {
                // Any ACK counts for broadcast
                this->ack = isAck;
                waitingForAck = false;
            } else if (isFromDest) {
                // Only ACK from destination counts as final
                this->ack = isAck;
                waitingForAck = false;
            } else if (isAck) {
                // Relay ACK → mark as RELAYED, still no final ACK
                this->ack = false;
                waitingForAck = false;
            } else {
                // Explicit failure
                this->ack = false;
                waitingForAck = false;
            }

            // Update last sent StoredMessage with ACK/NACK/RELAYED result
            if (!messageStore.getMessages().empty()) {
                StoredMessage &last = const_cast<StoredMessage &>(messageStore.getMessages().back());
                if (last.sender == nodeDB->getNodeNum()) { // only update our own messages
                    if (wasBroadcast && isAck) {
                        last.ackStatus = AckStatus::ACKED;
                    } else if (isFromDest && isAck) {
                        last.ackStatus = AckStatus::ACKED;
                    } else if (!isFromDest && isAck) {
                        last.ackStatus = AckStatus::RELAYED;
                    } else {
                        last.ackStatus = AckStatus::NACKED;
                    }
                }
            }

            // Capture radio metrics
            this->lastRxRssi = mp.rx_rssi;
            this->lastRxSnr = mp.rx_snr;

            // Show overlay banner
            if (screen) {
                auto *display = screen->getDisplayDevice();
                graphics::BannerOverlayOptions opts;
                static char buf[128];

                const char *channelName = channels.getName(this->channel);
                const char *src = getNodeName(this->incoming);
                char nodeName[48];
                strncpy(nodeName, src, sizeof(nodeName) - 1);
                nodeName[sizeof(nodeName) - 1] = '\0';

                int availWidth =
                    display->getWidth() - ((graphics::currentResolution == graphics::ScreenResolution::High) ? 60 : 30);
                if (availWidth < 0)
                    availWidth = 0;

                size_t origLen = strlen(nodeName);
                while (nodeName[0] && display->getStringWidth(nodeName) > availWidth) {
                    nodeName[strlen(nodeName) - 1] = '\0';
                }
                if (strlen(nodeName) < origLen) {
                    strcat(nodeName, "...");
                }

                // Calculate signal quality and bars based on preset, SNR, and RSSI
                float snrLimit = getSnrLimit(config.lora.modem_preset);
                int bars = 0;
                const char *qualityLabel = getSignalGrade(this->lastRxSnr, this->lastRxRssi, snrLimit, bars);

                if (this->ack) {
                    if (this->lastSentNode == NODENUM_BROADCAST) {
                        snprintf(buf, sizeof(buf), "Message sent to\n#%s\n\nSignal: %s",
                                 (channelName && channelName[0]) ? channelName : "unknown", qualityLabel);
                    } else {
                        snprintf(buf, sizeof(buf), "DM sent to\n@%s\n\nSignal: %s",
                                 (nodeName && nodeName[0]) ? nodeName : "unknown", qualityLabel);
                    }
                } else if (isAck && !isFromDest) {
                    // Relay ACK banner
                    snprintf(buf, sizeof(buf), "DM Relayed\n(Status Unknown)\n%s\n\nSignal: %s",
                             (nodeName && nodeName[0]) ? nodeName : "unknown", qualityLabel);
                } else {
                    if (this->lastSentNode == NODENUM_BROADCAST) {
                        snprintf(buf, sizeof(buf), "Message failed to\n#%s",
                                 (channelName && channelName[0]) ? channelName : "unknown");
                    } else {
                        snprintf(buf, sizeof(buf), "DM failed to\n@%s", (nodeName && nodeName[0]) ? nodeName : "unknown");
                    }
                }

                opts.message = buf;
                opts.durationMs = 3000;
                graphics::bannerSignalBars = bars; // tell banner renderer how many bars to draw
                screen->showOverlayBanner(opts);   // this triggers drawNotificationBox()
            }
        }
    }

    return ProcessMessage::CONTINUE;
}

#endif
