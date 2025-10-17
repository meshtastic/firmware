#include "configuration.h"
#if ARCH_PORTDUINO
#include "PortduinoGlue.h"
#endif
#if HAS_SCREEN
#include "CannedMessageModule.h"
#include "Channels.h"
#include "FSCommon.h"
#include "MeshService.h"
#include "MessageStore.h"
#include "NodeDB.h"
#include "SPILock.h"
#include "buzz.h"
#include "detect/ScanI2C.h"
#include "gps/RTC.h"
#include "graphics/Screen.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/draw/MessageRenderer.h"
#include "graphics/draw/NotificationRenderer.h"
#include "graphics/emotes.h"
#include "graphics/images.h"
#include "main.h" // for cardkb_found
#include "mesh/generated/meshtastic/cannedmessages.pb.h"
#include "modules/AdminModule.h"
#include "modules/ExternalNotificationModule.h" // for buzzer control
extern MessageStore messageStore;
#if HAS_TRACKBALL
#include "input/TrackballInterruptImpl1.h"
#endif
#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#endif
#if defined(USE_EINK) && defined(USE_EINK_DYNAMICDISPLAY)
#include "graphics/EInkDynamicDisplay.h" // To select between full and fast refresh on E-Ink displays
#endif

#ifndef INPUTBROKER_MATRIX_TYPE
#define INPUTBROKER_MATRIX_TYPE 0
#endif

#include "graphics/ScreenFonts.h"
#include <Throttle.h>

// Remove Canned message screen if no action is taken for some milliseconds
#define INACTIVATE_AFTER_MS 20000

// Tokenize a message string into emote/text segments
static std::vector<std::pair<bool, String>> tokenizeMessageWithEmotes(const char *msg)
{
    std::vector<std::pair<bool, String>> tokens;
    int msgLen = strlen(msg);
    int pos = 0;
    while (pos < msgLen) {
        const graphics::Emote *foundEmote = nullptr;
        int foundLen = 0;
        for (int j = 0; j < graphics::numEmotes; j++) {
            const char *label = graphics::emotes[j].label;
            int labelLen = strlen(label);
            if (labelLen == 0)
                continue;
            if (strncmp(msg + pos, label, labelLen) == 0) {
                if (!foundEmote || labelLen > foundLen) {
                    foundEmote = &graphics::emotes[j];
                    foundLen = labelLen;
                }
            }
        }
        if (foundEmote) {
            tokens.emplace_back(true, String(foundEmote->label));
            pos += foundLen;
        } else {
            // Find next emote
            int nextEmote = msgLen;
            for (int j = 0; j < graphics::numEmotes; j++) {
                const char *label = graphics::emotes[j].label;
                if (!label || !*label)
                    continue;
                const char *found = strstr(msg + pos, label);
                if (found && (found - msg) < nextEmote) {
                    nextEmote = found - msg;
                }
            }
            int textLen = (nextEmote > pos) ? (nextEmote - pos) : (msgLen - pos);
            if (textLen > 0) {
                tokens.emplace_back(false, String(msg + pos).substring(0, textLen));
                pos += textLen;
            } else {
                break;
            }
        }
    }
    return tokens;
}

// Render a single emote token centered vertically on a row
static void renderEmote(OLEDDisplay *display, int &nextX, int lineY, int rowHeight, const String &label)
{
    const graphics::Emote *emote = nullptr;
    for (int j = 0; j < graphics::numEmotes; j++) {
        if (label == graphics::emotes[j].label) {
            emote = &graphics::emotes[j];
            break;
        }
    }
    if (emote) {
        int emoteYOffset = (rowHeight - emote->height) / 2; // vertically center the emote
        display->drawXbm(nextX, lineY + emoteYOffset, emote->width, emote->height, emote->bitmap);
        nextX += emote->width + 2; // spacing between tokens
    }
}

namespace graphics
{
extern int bannerSignalBars;
}
extern ScanI2C::DeviceAddress cardkb_found;
extern bool graphics::isMuted;
extern bool osk_found;

static const char *cannedMessagesConfigFile = "/prefs/cannedConf.proto";
static NodeNum lastDest = NODENUM_BROADCAST;
static uint8_t lastChannel = 0;
static bool lastDestSet = false;

meshtastic_CannedMessageModuleConfig cannedMessageModuleConfig;

CannedMessageModule *cannedMessageModule;

CannedMessageModule::CannedMessageModule()
    : SinglePortModule("canned", meshtastic_PortNum_TEXT_MESSAGE_APP), concurrency::OSThread("CannedMessage")
{
    this->loadProtoForModule();
    if ((this->splitConfiguredMessages() <= 0) && (cardkb_found.address == 0x00) && !INPUTBROKER_MATRIX_TYPE &&
        !CANNED_MESSAGE_MODULE_ENABLE) {
        LOG_INFO("CannedMessageModule: No messages are configured. Module is disabled");
        this->runState = CANNED_MESSAGE_RUN_STATE_DISABLED;
        disable();
    } else {
        LOG_INFO("CannedMessageModule is enabled");
        moduleConfig.canned_message.enabled = true;
        this->inputObserver.observe(inputBroker);
    }
}

void CannedMessageModule::LaunchWithDestination(NodeNum newDest, uint8_t newChannel)
{
    // Do NOT override explicit broadcast replies
    // Only reuse lastDest in LaunchRepeatDestination()

    dest = newDest;
    channel = newChannel;

    lastDest = dest;
    lastChannel = channel;
    lastDestSet = true;

    // Upon activation, highlight "[Select Destination]"
    int selectDestination = 0;
    for (int i = 0; i < messagesCount; ++i) {
        if (strcmp(messages[i], "[Select Destination]") == 0) {
            selectDestination = i;
            break;
        }
    }
    currentMessageIndex = selectDestination;

    // This triggers the canned message list
    runState = CANNED_MESSAGE_RUN_STATE_ACTIVE;
    requestFocus();
    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    notifyObservers(&e);

    LOG_DEBUG("[CannedMessage] LaunchWithDestination dest=0x%08x ch=%d", dest, channel);
}

void CannedMessageModule::LaunchRepeatDestination()
{
    if (!lastDestSet) {
        LaunchWithDestination(NODENUM_BROADCAST, 0);
    } else {
        LaunchWithDestination(lastDest, lastChannel);
    }
}

void CannedMessageModule::LaunchFreetextWithDestination(NodeNum newDest, uint8_t newChannel)
{
    // Do NOT override explicit broadcast replies
    // Only reuse lastDest in LaunchRepeatDestination()

    dest = newDest;
    channel = newChannel;

    lastDest = dest;
    lastChannel = channel;
    lastDestSet = true;

    runState = CANNED_MESSAGE_RUN_STATE_FREETEXT;
    requestFocus();
    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    notifyObservers(&e);

    LOG_DEBUG("[CannedMessage] LaunchFreetextWithDestination dest=0x%08x ch=%d", dest, channel);
}

static bool returnToCannedList = false;
bool hasKeyForNode(const meshtastic_NodeInfoLite *node)
{
    return node && node->has_user && node->user.public_key.size > 0;
}
/**
 * @brief Items in array this->messages will be set to be pointing on the right
 *     starting points of the string this->messageStore
 *
 * @return int Returns the number of messages found.
 */

int CannedMessageModule::splitConfiguredMessages()
{
    int i = 0;

    String canned_messages = cannedMessageModuleConfig.messages;

    // Copy all message parts into the buffer
    strncpy(this->messageBuffer, canned_messages.c_str(), sizeof(this->messageBuffer));

    // Temporary array to allow for insertion
    const char *tempMessages[CANNED_MESSAGE_MODULE_MESSAGE_MAX_COUNT + 3] = {0};
    int tempCount = 0;
    // Insert at position 0 (top)
    tempMessages[tempCount++] = "[Select Destination]";
#if defined(USE_VIRTUAL_KEYBOARD)
    // Add a "Free Text" entry at the top if using a touch screen virtual keyboard
    tempMessages[tempCount++] = "[-- Free Text --]";
#else
    if (osk_found && screen) {
        tempMessages[tempCount++] = "[-- Free Text --]";
    }
#endif

    // First message always starts at buffer start
    tempMessages[tempCount++] = this->messageBuffer;
    int upTo = strlen(this->messageBuffer) - 1;

    // Walk buffer, splitting on '|'
    while (i < upTo) {
        if (this->messageBuffer[i] == '|') {
            this->messageBuffer[i] = '\0'; // End previous message
            if (tempCount >= CANNED_MESSAGE_MODULE_MESSAGE_MAX_COUNT)
                break;
            tempMessages[tempCount++] = (this->messageBuffer + i + 1);
        }
        i += 1;
    }

    // Add [Exit] as the last entry
    tempMessages[tempCount++] = "[Exit]";

    // Copy to the member array
    for (int k = 0; k < tempCount; ++k) {
        this->messages[k] = (char *)tempMessages[k];
    }
    this->messagesCount = tempCount;

    return this->messagesCount;
}
void CannedMessageModule::drawHeader(OLEDDisplay *display, int16_t x, int16_t y, char *buffer)
{
    if (graphics::isHighResolution) {
        if (this->dest == NODENUM_BROADCAST) {
            display->drawStringf(x, y, buffer, "To: #%s", channels.getName(this->channel));
        } else {
            display->drawStringf(x, y, buffer, "To: @%s", getNodeName(this->dest));
        }
    } else {
        if (this->dest == NODENUM_BROADCAST) {
            display->drawStringf(x, y, buffer, "To: #%.20s", channels.getName(this->channel));
        } else {
            display->drawStringf(x, y, buffer, "To: @%s", getNodeName(this->dest));
        }
    }
}

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

// Returns true if character input is currently allowed (used for search/freetext states)
bool CannedMessageModule::isCharInputAllowed() const
{
    return runState == CANNED_MESSAGE_RUN_STATE_FREETEXT || runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION;
}
/**
 * Main input event dispatcher for CannedMessageModule.
 * Routes keyboard/button/touch input to the correct handler based on the current runState.
 * Only one handler (per state) processes each event, eliminating redundancy.
 */
int CannedMessageModule::handleInputEvent(const InputEvent *event)
{
    // Block ALL input if an alert banner is active
    if (screen && screen->isOverlayBannerShowing()) {
        return 0;
    }

    // Tab key: Always allow switching between canned/destination screens
    if (event->kbchar == INPUT_BROKER_MSG_TAB && handleTabSwitch(event))
        return 1;

    // Matrix keypad: If matrix key, trigger action select for canned message
    if (event->inputEvent == INPUT_BROKER_MATRIXKEY) {
        runState = CANNED_MESSAGE_RUN_STATE_ACTION_SELECT;
        payload = INPUT_BROKER_MATRIXKEY;
        currentMessageIndex = event->kbchar - 1;
        lastTouchMillis = millis();
        requestFocus();
        return 1;
    }

    // Always normalize navigation/select buttons for further handlers
    bool isUp = isUpEvent(event);
    bool isDown = isDownEvent(event);
    bool isSelect = isSelectEvent(event);

    // Route event to handler for current UI state (no double-handling)
    switch (runState) {
    // Node/Channel destination selection mode: Handles character search, arrows, select, cancel, backspace
    case CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION:
        if (handleDestinationSelectionInput(event, isUp, isDown, isSelect))
            return 1;
        return 0; // prevent fall-through to selector input

    // Free text input mode: Handles character input, cancel, backspace, select, etc.
    case CANNED_MESSAGE_RUN_STATE_FREETEXT:
        return handleFreeTextInput(event); // All allowed input for this state

    // Virtual keyboard mode: Show virtual keyboard and handle input

    // If sending, block all input except global/system (handled above)
    case CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE:
        return 1;

    // If sending, block all input except global/system (handled above)
    case CANNED_MESSAGE_RUN_STATE_EMOTE_PICKER:
        return handleEmotePickerInput(event);

    case CANNED_MESSAGE_RUN_STATE_INACTIVE:
        if (isSelect) {
            return 0; // Main button press no longer runs through powerFSM
        }
        // Let LEFT/RIGHT pass through so frame navigation works
        if (event->inputEvent == INPUT_BROKER_LEFT || event->inputEvent == INPUT_BROKER_RIGHT) {
            break;
        }
        // Handle UP/DOWN: activate canned message list!
        if (event->inputEvent == INPUT_BROKER_UP || event->inputEvent == INPUT_BROKER_DOWN ||
            event->inputEvent == INPUT_BROKER_ALT_LONG) {
            LaunchWithDestination(NODENUM_BROADCAST);
            return 1;
        }
        // Printable char (ASCII) opens free text compose
        if (event->kbchar >= 32 && event->kbchar <= 126) {
            runState = CANNED_MESSAGE_RUN_STATE_FREETEXT;
            requestFocus();
            UIFrameEvent e;
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            notifyObservers(&e);
            // Immediately process the input in the new state (freetext)
            return handleFreeTextInput(event);
        }
        break;

    // (Other states can be added here as needed)
    default:
        break;
    }

    // If no state handler above processed the event, let the message selector try to handle it
    // (Handles up/down/select on canned message list, exit/return)
    if (handleMessageSelectorInput(event, isUp, isDown, isSelect))
        return 1;

    // Default: event not handled by canned message system, allow others to process
    return 0;
}

bool CannedMessageModule::isUpEvent(const InputEvent *event)
{
    return event->inputEvent == INPUT_BROKER_UP ||
           ((runState == CANNED_MESSAGE_RUN_STATE_ACTIVE || runState == CANNED_MESSAGE_RUN_STATE_EMOTE_PICKER ||
             runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION) &&
            (event->inputEvent == INPUT_BROKER_LEFT || event->inputEvent == INPUT_BROKER_ALT_PRESS));
}
bool CannedMessageModule::isDownEvent(const InputEvent *event)
{
    return event->inputEvent == INPUT_BROKER_DOWN ||
           ((runState == CANNED_MESSAGE_RUN_STATE_ACTIVE || runState == CANNED_MESSAGE_RUN_STATE_EMOTE_PICKER ||
             runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION) &&
            (event->inputEvent == INPUT_BROKER_RIGHT || event->inputEvent == INPUT_BROKER_USER_PRESS));
}
bool CannedMessageModule::isSelectEvent(const InputEvent *event)
{
    return event->inputEvent == INPUT_BROKER_SELECT;
}

bool CannedMessageModule::handleTabSwitch(const InputEvent *event)
{
    if (event->kbchar != 0x09)
        return false;

    runState = (runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION) ? CANNED_MESSAGE_RUN_STATE_FREETEXT
                                                                            : CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION;

    destIndex = 0;
    scrollIndex = 0;
    // RESTORE THIS!
    if (runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION)
        updateDestinationSelectionList();
    requestFocus();

    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    notifyObservers(&e);
    screen->forceDisplay();
    return true;
}

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
bool CannedMessageModule::handleFreeTextInput(const InputEvent *event)
{
    // Always process only if in FREETEXT mode
    if (runState != CANNED_MESSAGE_RUN_STATE_FREETEXT)
        return false;

#if defined(USE_VIRTUAL_KEYBOARD)
    // Cancel (dismiss freetext screen)
    if (event->inputEvent == INPUT_BROKER_LEFT) {
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
    // Touch input (virtual keyboard) handling
    // Only handle if touch coordinates present (CardKB won't set these)
    if (event->touchX != 0 || event->touchY != 0) {
        String keyTapped = keyForCoordinates(event->touchX, event->touchY);
        bool valid = false;

        if (keyTapped == "⇧") {
            highlight = -1;
            payload = 0x00;
            shift = !shift;
            valid = true;
        } else if (keyTapped == "⌫") {
#ifndef RAK14014
            highlight = keyTapped[0];
#endif
            payload = 0x08;
            shift = false;
            valid = true;
        } else if (keyTapped == "123" || keyTapped == "ABC") {
            highlight = -1;
            payload = 0x00;
            charSet = (charSet == 0 ? 1 : 0);
            valid = true;
        } else if (keyTapped == " ") {
#ifndef RAK14014
            highlight = keyTapped[0];
#endif
            payload = keyTapped[0];
            shift = false;
            valid = true;
        }
        // Touch enter/submit
        else if (keyTapped == "↵") {
            runState = CANNED_MESSAGE_RUN_STATE_ACTION_SELECT; // Send the message!
            payload = CANNED_MESSAGE_RUN_STATE_FREETEXT;
            currentMessageIndex = -1;
            shift = false;
            valid = true;
        } else if (!(keyTapped == "")) {
#ifndef RAK14014
            highlight = keyTapped[0];
#endif
            payload = shift ? keyTapped[0] : std::tolower(keyTapped[0]);
            shift = false;
            valid = true;
        }

        if (valid) {
            lastTouchMillis = millis();
            runOnce();
            payload = 0;
            return true; // STOP: We handled a VKB touch
        }
    }
#endif // USE_VIRTUAL_KEYBOARD

    // All hardware keys fall through to here (CardKB, physical, etc.)

    if (event->kbchar == INPUT_BROKER_MSG_EMOTE_LIST) {
        runState = CANNED_MESSAGE_RUN_STATE_EMOTE_PICKER;
        requestFocus();
        screen->forceDisplay();
        return true;
    }
    // Confirm select (Enter)
    bool isSelect = isSelectEvent(event);
    if (isSelect) {
        LOG_DEBUG("[SELECT] handleFreeTextInput: runState=%d, dest=%u, channel=%d, freetext='%s'", (int)runState, dest, channel,
                  freetext.c_str());
        if (dest == 0)
            dest = NODENUM_BROADCAST;
        // Defensive: If channel isn't valid, pick the first available channel
        if (channel >= channels.getNumChannels())
            channel = 0;

        payload = CANNED_MESSAGE_RUN_STATE_FREETEXT;
        currentMessageIndex = -1;
        runState = CANNED_MESSAGE_RUN_STATE_ACTION_SELECT;
        lastTouchMillis = millis();
        runOnce();
        return true;
    }

    // Backspace
    if (event->inputEvent == INPUT_BROKER_BACK && this->freetext.length() > 0) {
        payload = 0x08;
        lastTouchMillis = millis();
        runOnce();
        return true;
    }

    // Move cursor left
    if (event->inputEvent == INPUT_BROKER_LEFT) {
        payload = INPUT_BROKER_LEFT;
        lastTouchMillis = millis();
        runOnce();
        return true;
    }
    // Move cursor right
    if (event->inputEvent == INPUT_BROKER_RIGHT) {
        payload = INPUT_BROKER_RIGHT;
        lastTouchMillis = millis();
        runOnce();
        return true;
    }

    // Cancel (dismiss freetext screen)
    if (event->inputEvent == INPUT_BROKER_CANCEL || event->inputEvent == INPUT_BROKER_ALT_LONG ||
        (event->inputEvent == INPUT_BROKER_BACK && this->freetext.length() == 0)) {
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

    // Tab (switch destination)
    if (event->kbchar == INPUT_BROKER_MSG_TAB) {
        return handleTabSwitch(event); // Reuse tab logic
    }

    // Printable ASCII (add char to draft)
    if (event->kbchar >= 32 && event->kbchar <= 126) {
        payload = event->kbchar;
        lastTouchMillis = millis();
        runOnce();
        return true;
    }

    return false;
}

int CannedMessageModule::handleEmotePickerInput(const InputEvent *event)
{
    int numEmotes = graphics::numEmotes;

    // Override isDown and isSelect ONLY for emote picker behavior
    bool isUp = isUpEvent(event);
    bool isDown = isDownEvent(event);
    bool isSelect = isSelectEvent(event);
    if (runState == CANNED_MESSAGE_RUN_STATE_EMOTE_PICKER) {
        if (event->inputEvent == INPUT_BROKER_USER_PRESS) {
            isDown = true;
        } else if (event->inputEvent == INPUT_BROKER_SELECT) {
            isSelect = true;
        }
    }

    // Scroll emote list
    if (isUp && emotePickerIndex > 0) {
        emotePickerIndex--;
        screen->forceDisplay();
        return 1;
    }
    if (isDown && emotePickerIndex < numEmotes - 1) {
        emotePickerIndex++;
        screen->forceDisplay();
        return 1;
    }

    // Select emote: insert into freetext at cursor and return to freetext
    if (isSelect) {
        String label = graphics::emotes[emotePickerIndex].label;
        String emoteInsert = label; // Just the text label, e.g., ":thumbsup:"
        if (cursor == freetext.length()) {
            freetext += emoteInsert;
        } else {
            freetext = freetext.substring(0, cursor) + emoteInsert + freetext.substring(cursor);
        }
        cursor += emoteInsert.length();
        runState = CANNED_MESSAGE_RUN_STATE_FREETEXT;
        screen->forceDisplay();
        return 1;
    }

    // Cancel returns to freetext
    if (event->inputEvent == INPUT_BROKER_CANCEL || event->inputEvent == INPUT_BROKER_ALT_LONG) {
        runState = CANNED_MESSAGE_RUN_STATE_FREETEXT;
        screen->forceDisplay();
        return 1;
    }

    return 0;
}

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

int32_t CannedMessageModule::runOnce()
{
    if (this->runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION && needsUpdate) {
        updateDestinationSelectionList();
        needsUpdate = false;
    }

    // If we're in node selection, do nothing except keep alive
    if (this->runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION) {
        return INACTIVATE_AFTER_MS;
    }

    // Normal module disable/idle handling
    if ((this->runState == CANNED_MESSAGE_RUN_STATE_DISABLED) || (this->runState == CANNED_MESSAGE_RUN_STATE_INACTIVE)) {
        // Clean up virtual keyboard if needed when going inactive
        if (graphics::NotificationRenderer::virtualKeyboard && graphics::NotificationRenderer::textInputCallback == nullptr) {
            LOG_INFO("Performing delayed virtual keyboard cleanup");
            delete graphics::NotificationRenderer::virtualKeyboard;
            graphics::NotificationRenderer::virtualKeyboard = nullptr;
        }

        return INT32_MAX;
    }

    // Handle delayed virtual keyboard message sending
    if (this->runState == CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE && this->payload == CANNED_MESSAGE_RUN_STATE_FREETEXT) {
        // Virtual keyboard message sending case - text was not empty
        if (this->freetext.length() > 0) {
            LOG_INFO("Processing delayed virtual keyboard send: '%s'", this->freetext.c_str());
            sendText(this->dest, this->channel, this->freetext.c_str(), true);

            // Clean up virtual keyboard after sending
            if (graphics::NotificationRenderer::virtualKeyboard) {
                LOG_INFO("Cleaning up virtual keyboard after message send");
                delete graphics::NotificationRenderer::virtualKeyboard;
                graphics::NotificationRenderer::virtualKeyboard = nullptr;
                graphics::NotificationRenderer::textInputCallback = nullptr;
                graphics::NotificationRenderer::resetBanner();
            }

            // Clear payload to indicate virtual keyboard processing is complete
            // But keep SENDING_ACTIVE state to show "Sending..." screen for 2 seconds
            this->payload = 0;
        } else {
            // Empty message, just go inactive
            LOG_INFO("Empty freetext detected in delayed processing, returning to inactive state");
            this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
        }

        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        this->currentMessageIndex = -1;
        this->freetext = "";
        this->cursor = 0;
        this->notifyObservers(&e);
        return 2000;
    }

    UIFrameEvent e;
    if ((this->runState == CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE && this->payload != 0 &&
         this->payload != CANNED_MESSAGE_RUN_STATE_FREETEXT) ||
        (this->runState == CANNED_MESSAGE_RUN_STATE_ACK_NACK_RECEIVED) ||
        (this->runState == CANNED_MESSAGE_RUN_STATE_MESSAGE_SELECTION)) {
        this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        this->currentMessageIndex = -1;
        this->freetext = "";
        this->cursor = 0;
        this->notifyObservers(&e);
    }
    // Handle SENDING_ACTIVE state transition after virtual keyboard message
    else if (this->runState == CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE && this->payload == 0) {
        this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
        this->currentMessageIndex = -1;
        this->freetext = "";
        this->cursor = 0;
        return INT32_MAX;
    } else if (((this->runState == CANNED_MESSAGE_RUN_STATE_ACTIVE) || (this->runState == CANNED_MESSAGE_RUN_STATE_FREETEXT)) &&
               !Throttle::isWithinTimespanMs(this->lastTouchMillis, INACTIVATE_AFTER_MS)) {
        // Reset module on inactivity
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        this->currentMessageIndex = -1;
        this->freetext = "";
        this->cursor = 0;
        this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;

        // Clean up virtual keyboard if it exists during timeout
        if (graphics::NotificationRenderer::virtualKeyboard) {
            LOG_INFO("Cleaning up virtual keyboard due to module timeout");
            delete graphics::NotificationRenderer::virtualKeyboard;
            graphics::NotificationRenderer::virtualKeyboard = nullptr;
            graphics::NotificationRenderer::textInputCallback = nullptr;
            graphics::NotificationRenderer::resetBanner();
        }

        this->notifyObservers(&e);
    } else if (this->runState == CANNED_MESSAGE_RUN_STATE_ACTION_SELECT) {
        if (this->payload == 0) {
            // [Exit] button pressed - return to inactive state
            LOG_INFO("Processing [Exit] action - returning to inactive state");
            this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
        } else if (this->payload == CANNED_MESSAGE_RUN_STATE_FREETEXT) {
            if (this->freetext.length() > 0) {
                sendText(this->dest, this->channel, this->freetext.c_str(), true);

                // Clean up state but *don’t* deactivate yet
                this->currentMessageIndex = -1;
                this->freetext = "";
                this->cursor = 0;

                // Tell Screen to jump straight to the TextMessage frame
                UIFrameEvent e;
                e.action = UIFrameEvent::Action::SWITCH_TO_TEXTMESSAGE;
                this->notifyObservers(&e);

                // Now deactivate this module
                this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;

                return INT32_MAX; // don’t fall back into canned list
            } else {
                this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
            }
        } else {
            if (strcmp(this->messages[this->currentMessageIndex], "[Select Destination]") == 0) {
                this->runState = CANNED_MESSAGE_RUN_STATE_ACTIVE;
                return INT32_MAX;
            }
            if ((this->messagesCount > this->currentMessageIndex) && (strlen(this->messages[this->currentMessageIndex]) > 0)) {
                if (strcmp(this->messages[this->currentMessageIndex], "~") == 0) {
                    return INT32_MAX;
                } else {
                    sendText(this->dest, this->channel, this->messages[this->currentMessageIndex], true);

                    // Clean up state
                    this->currentMessageIndex = -1;
                    this->freetext = "";
                    this->cursor = 0;

                    // Tell Screen to jump straight to the TextMessage frame
                    UIFrameEvent e;
                    e.action = UIFrameEvent::Action::SWITCH_TO_TEXTMESSAGE;
                    this->notifyObservers(&e);

                    // Now deactivate this module
                    this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;

                    return INT32_MAX; // don’t fall back into canned list
                }
            } else {
                this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
            }
        }
        // fallback clean-up if nothing above returned
        this->currentMessageIndex = -1;
        this->freetext = "";
        this->cursor = 0;

        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        this->notifyObservers(&e);

        // Immediately stop, don’t linger on canned screen
        return INT32_MAX;
    }
    // Highlight [Select Destination] initially when entering the message list
    else if ((this->runState != CANNED_MESSAGE_RUN_STATE_FREETEXT) && (this->currentMessageIndex == -1)) {
        // Only auto-highlight [Select Destination] if we’re ACTIVELY browsing,
        // not when coming back from a sent message.
        if (this->runState == CANNED_MESSAGE_RUN_STATE_ACTIVE) {
            int selectDestination = 0;
            for (int i = 0; i < this->messagesCount; ++i) {
                if (strcmp(this->messages[i], "[Select Destination]") == 0) {
                    selectDestination = i;
                    break;
                }
            }
            this->currentMessageIndex = selectDestination;
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        }
    } else if (this->runState == CANNED_MESSAGE_RUN_STATE_ACTION_UP) {
        if (this->messagesCount > 0) {
            this->currentMessageIndex = getPrevIndex();
            this->freetext = "";
            this->cursor = 0;
            this->runState = CANNED_MESSAGE_RUN_STATE_ACTIVE;
        }
    } else if (this->runState == CANNED_MESSAGE_RUN_STATE_ACTION_DOWN) {
        if (this->messagesCount > 0) {
            this->currentMessageIndex = this->getNextIndex();
            this->freetext = "";
            this->cursor = 0;
            this->runState = CANNED_MESSAGE_RUN_STATE_ACTIVE;
        }
    } else if (this->runState == CANNED_MESSAGE_RUN_STATE_FREETEXT || this->runState == CANNED_MESSAGE_RUN_STATE_ACTIVE) {
        switch (this->payload) {
        case INPUT_BROKER_LEFT:
            if (this->runState == CANNED_MESSAGE_RUN_STATE_FREETEXT && this->cursor > 0) {
                this->cursor--;
            }
            break;
        case INPUT_BROKER_RIGHT:
            if (this->runState == CANNED_MESSAGE_RUN_STATE_FREETEXT && this->cursor < this->freetext.length()) {
                this->cursor++;
            }
            break;
        default:
            break;
        }
        if (this->runState == CANNED_MESSAGE_RUN_STATE_FREETEXT) {
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            switch (this->payload) {
            case 0x08: // backspace
                if (this->freetext.length() > 0) {
                    if (this->cursor > 0) {
                        if (this->cursor == this->freetext.length()) {
                            this->freetext = this->freetext.substring(0, this->freetext.length() - 1);
                        } else {
                            this->freetext = this->freetext.substring(0, this->cursor - 1) +
                                             this->freetext.substring(this->cursor, this->freetext.length());
                        }
                        this->cursor--;
                    }
                } else {
                }
                break;
            case INPUT_BROKER_MSG_TAB: // Tab key: handled by input handler
                return 0;
            case INPUT_BROKER_LEFT:
            case INPUT_BROKER_RIGHT:
                break;
            default:
                // Only insert ASCII printable characters (32–126)
                if (this->payload >= 32 && this->payload <= 126) {
                    requestFocus();
                    if (this->cursor == this->freetext.length()) {
                        this->freetext += (char)this->payload;
                    } else {
                        this->freetext = this->freetext.substring(0, this->cursor) + (char)this->payload +
                                         this->freetext.substring(this->cursor);
                    }
                    this->cursor++;
                    const uint16_t maxChars = 200 - (moduleConfig.canned_message.send_bell ? 1 : 0);
                    if (this->freetext.length() > maxChars) {
                        this->cursor = maxChars;
                        this->freetext = this->freetext.substring(0, maxChars);
                    }
                }
                break;
            }
        }
        this->lastTouchMillis = millis();
        this->notifyObservers(&e);
        return INACTIVATE_AFTER_MS;
    }

    if (this->runState == CANNED_MESSAGE_RUN_STATE_ACTIVE) {
        this->lastTouchMillis = millis();
        this->notifyObservers(&e);
        return INACTIVATE_AFTER_MS;
    }
    return INT32_MAX;
}

const char *CannedMessageModule::getCurrentMessage()
{
    return this->messages[this->currentMessageIndex];
}
const char *CannedMessageModule::getPrevMessage()
{
    return this->messages[this->getPrevIndex()];
}
const char *CannedMessageModule::getNextMessage()
{
    return this->messages[this->getNextIndex()];
}
const char *CannedMessageModule::getMessageByIndex(int index)
{
    return (index >= 0 && index < this->messagesCount) ? this->messages[index] : "";
}

const char *CannedMessageModule::getNodeName(NodeNum node)
{
    if (node == NODENUM_BROADCAST)
        return "Broadcast";

    meshtastic_NodeInfoLite *info = nodeDB->getMeshNode(node);
    if (info && info->has_user && strlen(info->user.long_name) > 0) {
        return info->user.long_name;
    }

    static char fallback[12];
    snprintf(fallback, sizeof(fallback), "0x%08x", node);
    return fallback;
}

bool CannedMessageModule::shouldDraw()
{
    // Only allow drawing when we're in an interactive UI state.
    return (this->runState == CANNED_MESSAGE_RUN_STATE_ACTIVE || this->runState == CANNED_MESSAGE_RUN_STATE_FREETEXT ||
            this->runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION ||
            this->runState == CANNED_MESSAGE_RUN_STATE_EMOTE_PICKER);
}

// Has the user defined any canned messages?
// Expose publicly whether canned message module is ready for use
bool CannedMessageModule::hasMessages()
{
    return (this->messagesCount > 0);
}

int CannedMessageModule::getNextIndex()
{
    if (this->currentMessageIndex >= (this->messagesCount - 1)) {
        return 0;
    } else {
        return this->currentMessageIndex + 1;
    }
}

int CannedMessageModule::getPrevIndex()
{
    if (this->currentMessageIndex <= 0) {
        return this->messagesCount - 1;
    } else {
        return this->currentMessageIndex - 1;
    }
}

#if defined(USE_VIRTUAL_KEYBOARD)

String CannedMessageModule::keyForCoordinates(uint x, uint y)
{
    int outerSize = *(&this->keyboard[this->charSet] + 1) - this->keyboard[this->charSet];

    for (int8_t outerIndex = 0; outerIndex < outerSize; outerIndex++) {
        int innerSize = *(&this->keyboard[this->charSet][outerIndex] + 1) - this->keyboard[this->charSet][outerIndex];

        for (int8_t innerIndex = 0; innerIndex < innerSize; innerIndex++) {
            Letter letter = this->keyboard[this->charSet][outerIndex][innerIndex];

            if (x > letter.rectX && x < (letter.rectX + letter.rectWidth) && y > letter.rectY &&
                y < (letter.rectY + letter.rectHeight)) {
                return letter.character;
            }
        }
    }

    return "";
}

void CannedMessageModule::drawKeyboard(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    int outerSize = *(&this->keyboard[this->charSet] + 1) - this->keyboard[this->charSet];

    int xOffset = 0;

    int yOffset = 56;

    display->setTextAlignment(TEXT_ALIGN_LEFT);

    display->setFont(FONT_SMALL);

    display->setColor(OLEDDISPLAY_COLOR::WHITE);

    display->drawStringMaxWidth(0, 0, display->getWidth(),
                                cannedMessageModule->drawWithCursor(cannedMessageModule->freetext, cannedMessageModule->cursor));

    display->setFont(FONT_MEDIUM);

    int cellHeight = round((display->height() - 64) / outerSize);

    int yCorrection = 8;

    for (int8_t outerIndex = 0; outerIndex < outerSize; outerIndex++) {
        yOffset += outerIndex > 0 ? cellHeight : 0;

        int innerSizeBound = *(&this->keyboard[this->charSet][outerIndex] + 1) - this->keyboard[this->charSet][outerIndex];

        int innerSize = 0;

        for (int8_t innerIndex = 0; innerIndex < innerSizeBound; innerIndex++) {
            if (this->keyboard[this->charSet][outerIndex][innerIndex].character != "") {
                innerSize++;
            }
        }

        int cellWidth = display->width() / innerSize;

        for (int8_t innerIndex = 0; innerIndex < innerSize; innerIndex++) {
            xOffset += innerIndex > 0 ? cellWidth : 0;

            Letter letter = this->keyboard[this->charSet][outerIndex][innerIndex];

            Letter updatedLetter = {letter.character, letter.width, xOffset, yOffset, cellWidth, cellHeight};

#ifdef RAK14014 // Optimize the touch range of the virtual keyboard in the bottom row
            if (outerIndex == outerSize - 1) {
                updatedLetter.rectHeight = 240 - yOffset;
            }
#endif
            this->keyboard[this->charSet][outerIndex][innerIndex] = updatedLetter;

            float characterOffset = ((cellWidth / 2) - (letter.width / 2));

            if (letter.character == "⇧") {
                if (this->shift) {
                    display->fillRect(xOffset, yOffset, cellWidth, cellHeight);

                    display->setColor(OLEDDISPLAY_COLOR::BLACK);

                    drawShiftIcon(display, xOffset + characterOffset, yOffset + yCorrection + 5, 1.2);

                    display->setColor(OLEDDISPLAY_COLOR::WHITE);
                } else {
                    display->drawRect(xOffset, yOffset, cellWidth, cellHeight);

                    drawShiftIcon(display, xOffset + characterOffset, yOffset + yCorrection + 5, 1.2);
                }
            } else if (letter.character == "⌫") {
                if (this->highlight == letter.character[0]) {
                    display->fillRect(xOffset, yOffset, cellWidth, cellHeight);

                    display->setColor(OLEDDISPLAY_COLOR::BLACK);

                    drawBackspaceIcon(display, xOffset + characterOffset, yOffset + yCorrection + 5, 1.2);

                    display->setColor(OLEDDISPLAY_COLOR::WHITE);

                    setIntervalFromNow(0);
                } else {
                    display->drawRect(xOffset, yOffset, cellWidth, cellHeight);

                    drawBackspaceIcon(display, xOffset + characterOffset, yOffset + yCorrection + 5, 1.2);
                }
            } else if (letter.character == "↵") {
                display->drawRect(xOffset, yOffset, cellWidth, cellHeight);

                drawEnterIcon(display, xOffset + characterOffset, yOffset + yCorrection + 5, 1.7);
            } else {
                if (this->highlight == letter.character[0]) {
                    display->fillRect(xOffset, yOffset, cellWidth, cellHeight);

                    display->setColor(OLEDDISPLAY_COLOR::BLACK);

                    display->drawString(xOffset + characterOffset, yOffset + yCorrection,
                                        letter.character == " " ? "space" : letter.character);

                    display->setColor(OLEDDISPLAY_COLOR::WHITE);

                    setIntervalFromNow(0);
                } else {
                    display->drawRect(xOffset, yOffset, cellWidth, cellHeight);

                    display->drawString(xOffset + characterOffset, yOffset + yCorrection,
                                        letter.character == " " ? "space" : letter.character);
                }
            }
        }

        xOffset = 0;
    }

    this->highlight = 0x00;
}

void CannedMessageModule::drawShiftIcon(OLEDDisplay *display, int x, int y, float scale)
{
    PointStruct shiftIcon[10] = {{8, 0}, {15, 7}, {15, 8}, {12, 8}, {12, 12}, {4, 12}, {4, 8}, {1, 8}, {1, 7}, {8, 0}};

    int size = 10;

    for (int i = 0; i < size - 1; i++) {
        int x0 = x + (shiftIcon[i].x * scale);
        int y0 = y + (shiftIcon[i].y * scale);
        int x1 = x + (shiftIcon[i + 1].x * scale);
        int y1 = y + (shiftIcon[i + 1].y * scale);

        display->drawLine(x0, y0, x1, y1);
    }
}

void CannedMessageModule::drawBackspaceIcon(OLEDDisplay *display, int x, int y, float scale)
{
    PointStruct backspaceIcon[6] = {{0, 7}, {5, 2}, {15, 2}, {15, 12}, {5, 12}, {0, 7}};

    int size = 6;

    for (int i = 0; i < size - 1; i++) {
        int x0 = x + (backspaceIcon[i].x * scale);
        int y0 = y + (backspaceIcon[i].y * scale);
        int x1 = x + (backspaceIcon[i + 1].x * scale);
        int y1 = y + (backspaceIcon[i + 1].y * scale);

        display->drawLine(x0, y0, x1, y1);
    }

    PointStruct backspaceIconX[4] = {{7, 4}, {13, 10}, {7, 10}, {13, 4}};

    size = 4;

    for (int i = 0; i < size - 1; i++) {
        int x0 = x + (backspaceIconX[i].x * scale);
        int y0 = y + (backspaceIconX[i].y * scale);
        int x1 = x + (backspaceIconX[i + 1].x * scale);
        int y1 = y + (backspaceIconX[i + 1].y * scale);

        display->drawLine(x0, y0, x1, y1);
    }
}

void CannedMessageModule::drawEnterIcon(OLEDDisplay *display, int x, int y, float scale)
{
    PointStruct enterIcon[6] = {{0, 7}, {4, 3}, {4, 11}, {0, 7}, {15, 7}, {15, 0}};

    int size = 6;

    for (int i = 0; i < size - 1; i++) {
        int x0 = x + (enterIcon[i].x * scale);
        int y0 = y + (enterIcon[i].y * scale);
        int x1 = x + (enterIcon[i + 1].x * scale);
        int y1 = y + (enterIcon[i + 1].y * scale);

        display->drawLine(x0, y0, x1, y1);
    }
}

#endif

// Indicate to screen class that module is handling keyboard input specially (at certain times)
// This prevents the left & right keys being used for nav. between screen frames during text entry.
bool CannedMessageModule::interceptingKeyboardInput()
{
    switch (runState) {
    case CANNED_MESSAGE_RUN_STATE_DISABLED:
    case CANNED_MESSAGE_RUN_STATE_INACTIVE:
        return false;
    default:
        return true;
    }
}

// Draw the node/channel selection screen
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
                if (node) {
                    if (node->is_favorite) {
#if defined(M5STACK_UNITC6L)
                        snprintf(entryText, sizeof(entryText), "* %s", node->user.short_name);
                    } else {
                        snprintf(entryText, sizeof(entryText), "%s", node->user.short_name);
                    }
#else
                        snprintf(entryText, sizeof(entryText), "* %s", node->user.long_name);
                    } else {
                        snprintf(entryText, sizeof(entryText), "%s", node->user.long_name);
                    }
#endif
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

void CannedMessageModule::drawEmotePickerScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    const int headerFontHeight = FONT_HEIGHT_SMALL; // Make sure this matches your actual small font height
    const int headerMargin = 2;                     // Extra pixels below header
    const int labelGap = 6;
    const int bitmapGapX = 4;

    // Find max emote height (assume all same, or precalculated)
    int maxEmoteHeight = 0;
    for (int i = 0; i < graphics::numEmotes; ++i)
        if (graphics::emotes[i].height > maxEmoteHeight)
            maxEmoteHeight = graphics::emotes[i].height;

    const int rowHeight = maxEmoteHeight + 2;

    // Place header at top, then compute start of emote list
    int headerY = y;
    int listTop = headerY + headerFontHeight + headerMargin;

    int _visibleRows = (display->getHeight() - listTop - 2) / rowHeight;
    int numEmotes = graphics::numEmotes;

    // keep member variable in sync
    this->visibleRows = _visibleRows;

    // Clamp highlight index
    if (emotePickerIndex < 0)
        emotePickerIndex = 0;
    if (emotePickerIndex >= numEmotes)
        emotePickerIndex = numEmotes - 1;

    // Determine which emote is at the top
    int topIndex = emotePickerIndex - _visibleRows / 2;
    if (topIndex < 0)
        topIndex = 0;
    if (topIndex > numEmotes - _visibleRows)
        topIndex = std::max(0, numEmotes - _visibleRows);

    // Draw header/title
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(display->getWidth() / 2, headerY, "Select Emote");

    // Draw emote rows
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    for (int vis = 0; vis < _visibleRows; ++vis) {
        int emoteIdx = topIndex + vis;
        if (emoteIdx >= numEmotes)
            break;
        const graphics::Emote &emote = graphics::emotes[emoteIdx];
        int rowY = listTop + vis * rowHeight;

        // Draw highlight box 2px taller than emote (1px margin above and below)
        if (emoteIdx == emotePickerIndex) {
            display->fillRect(x, rowY, display->getWidth() - 8, emote.height + 2);
            display->setColor(BLACK);
        }

        // Emote bitmap (left), 1px margin from highlight bar top
        int emoteY = rowY + 1;
        display->drawXbm(x + bitmapGapX, emoteY, emote.width, emote.height, emote.bitmap);

        // Emote label (right of bitmap)
        display->setFont(FONT_MEDIUM);
        int labelY = rowY + ((rowHeight - FONT_HEIGHT_MEDIUM) / 2);
        display->drawString(x + bitmapGapX + emote.width + labelGap, labelY, emote.label);

        if (emoteIdx == emotePickerIndex)
            display->setColor(WHITE);
    }

    // Draw scrollbar if needed
    if (numEmotes > _visibleRows) {
        int scrollbarHeight = _visibleRows * rowHeight;
        int scrollTrackX = display->getWidth() - 6;
        display->drawRect(scrollTrackX, listTop, 4, scrollbarHeight);
        int scrollBarLen = std::max(6, (scrollbarHeight * _visibleRows) / numEmotes);
        int scrollBarPos = listTop + (scrollbarHeight * topIndex) / numEmotes;
        display->fillRect(scrollTrackX, scrollBarPos, 4, scrollBarLen);
    }
}

void CannedMessageModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    this->displayHeight = display->getHeight(); // Store display height for later use
    char buffer[50];
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    // Never draw if state is outside our UI modes
    if (!(runState == CANNED_MESSAGE_RUN_STATE_ACTIVE || runState == CANNED_MESSAGE_RUN_STATE_FREETEXT ||
          runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION || runState == CANNED_MESSAGE_RUN_STATE_EMOTE_PICKER)) {
        return; // bail if not in a UI state that should render
    }

    // Emote Picker Screen
    if (this->runState == CANNED_MESSAGE_RUN_STATE_EMOTE_PICKER) {
        drawEmotePickerScreen(display, state, x, y); // <-- Call your emote picker drawer here
        return;
    }

    // Destination Selection
    if (this->runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION) {
        drawDestinationSelectionScreen(display, state, x, y);
        return;
    }

    // Disabled Screen
    if (this->runState == CANNED_MESSAGE_RUN_STATE_DISABLED) {
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->setFont(FONT_SMALL);
        display->drawString(10 + x, 0 + y + FONT_HEIGHT_SMALL, "Canned Message\nModule disabled.");
        return;
    }

    // Free Text Input Screen
    if (this->runState == CANNED_MESSAGE_RUN_STATE_FREETEXT) {
        requestFocus();
#if defined(USE_EINK) && defined(USE_EINK_DYNAMICDISPLAY)
        EInkDynamicDisplay *einkDisplay = static_cast<EInkDynamicDisplay *>(display);
        einkDisplay->enableUnlimitedFastMode();
#endif
#if defined(USE_VIRTUAL_KEYBOARD)
        drawKeyboard(display, state, 0, 0);
#else
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->setFont(FONT_SMALL);

        // Draw node/channel header at the top
        drawHeader(display, x, y, buffer);

        // Char count right-aligned
        if (runState != CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION) {
            uint16_t charsLeft =
                meshtastic_Constants_DATA_PAYLOAD_LEN - this->freetext.length() - (moduleConfig.canned_message.send_bell ? 1 : 0);
            snprintf(buffer, sizeof(buffer), "%d left", charsLeft);
            display->drawString(x + display->getWidth() - display->getStringWidth(buffer), y + 0, buffer);
        }

        // Draw Free Text input with multi-emote support and proper line wrapping
        display->setColor(WHITE);
        {
            int inputY = 0 + y + FONT_HEIGHT_SMALL;
            String msgWithCursor = this->drawWithCursor(this->freetext, this->cursor);

            // Tokenize input into (isEmote, token) pairs
            const char *msg = msgWithCursor.c_str();
            std::vector<std::pair<bool, String>> tokens = tokenizeMessageWithEmotes(msg);

            // Advanced word-wrapping (emotes + text, split by word, wrap inside word if needed)
            std::vector<std::vector<std::pair<bool, String>>> lines;
            std::vector<std::pair<bool, String>> currentLine;
            int lineWidth = 0;
            int maxWidth = display->getWidth();
            for (auto &token : tokens) {
                if (token.first) {
                    // Emote
                    int tokenWidth = 0;
                    for (int j = 0; j < graphics::numEmotes; j++) {
                        if (token.second == graphics::emotes[j].label) {
                            tokenWidth = graphics::emotes[j].width + 2;
                            break;
                        }
                    }
                    if (lineWidth + tokenWidth > maxWidth && !currentLine.empty()) {
                        lines.push_back(currentLine);
                        currentLine.clear();
                        lineWidth = 0;
                    }
                    currentLine.push_back(token);
                    lineWidth += tokenWidth;
                } else {
                    // Text: split by words and wrap inside word if needed
                    String text = token.second;
                    int pos = 0;
                    while (pos < static_cast<int>(text.length())) {
                        // Find next space (or end)
                        int spacePos = text.indexOf(' ', pos);
                        int endPos = (spacePos == -1) ? text.length() : spacePos + 1; // Include space
                        String word = text.substring(pos, endPos);
                        int wordWidth = display->getStringWidth(word);

                        if (lineWidth + wordWidth > maxWidth && lineWidth > 0) {
                            lines.push_back(currentLine);
                            currentLine.clear();
                            lineWidth = 0;
                        }
                        // If word itself too big, split by character
                        if (wordWidth > maxWidth) {
                            uint16_t charPos = 0;
                            while (charPos < word.length()) {
                                String oneChar = word.substring(charPos, charPos + 1);
                                int charWidth = display->getStringWidth(oneChar);
                                if (lineWidth + charWidth > maxWidth && lineWidth > 0) {
                                    lines.push_back(currentLine);
                                    currentLine.clear();
                                    lineWidth = 0;
                                }
                                currentLine.push_back({false, oneChar});
                                lineWidth += charWidth;
                                charPos++;
                            }
                        } else {
                            currentLine.push_back({false, word});
                            lineWidth += wordWidth;
                        }
                        pos = endPos;
                    }
                }
            }
            if (!currentLine.empty())
                lines.push_back(currentLine);

            // Draw lines with emotes
            int rowHeight = FONT_HEIGHT_SMALL;
            int yLine = inputY;
            for (auto &line : lines) {
                int nextX = x;
                for (const auto &token : line) {
                    if (token.first) {
                        // Emote rendering centralized in helper
                        renderEmote(display, nextX, yLine, rowHeight, token.second);
                    } else {
                        display->drawString(nextX, yLine, token.second);
                        nextX += display->getStringWidth(token.second);
                    }
                }
                yLine += rowHeight;
            }
        }
#endif
        return;
    }

    // Canned Messages List
    if (this->messagesCount > 0) {
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->setFont(FONT_SMALL);

        // Precompute per-row heights based on emotes (centered if present)
        const int baseRowSpacing = FONT_HEIGHT_SMALL - 4;

        int topMsg;
        std::vector<int> rowHeights;
        int _visibleRows;

        // Draw header (To: ...)
        drawHeader(display, x, y, buffer);

        // Shift message list upward by 3 pixels to reduce spacing between header and first message
        const int listYOffset = y + FONT_HEIGHT_SMALL - 3;
        _visibleRows = (display->getHeight() - listYOffset) / baseRowSpacing;

        // Figure out which messages are visible and their needed heights
        topMsg = (messagesCount > _visibleRows && currentMessageIndex >= _visibleRows - 1)
                     ? currentMessageIndex - _visibleRows + 2
                     : 0;
        int countRows = std::min(messagesCount, _visibleRows);

        // Build per-row max height based on all emotes in line
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
                    search += strlen(label); // Advance past this emote
                }
            }
            rowHeights.push_back(std::max(baseRowSpacing, maxEmoteHeight + 2));
        }

        // Draw all message rows with multi-emote support
        int yCursor = listYOffset;
        for (int vis = 0; vis < countRows; vis++) {
            int msgIdx = topMsg + vis;
            int lineY = yCursor;
            const char *msg = getMessageByIndex(msgIdx);
            int rowHeight = rowHeights[vis];
            bool _highlight = (msgIdx == currentMessageIndex);

            // Multi-emote tokenization
            std::vector<std::pair<bool, String>> tokens = tokenizeMessageWithEmotes(msg);

            // Vertically center based on rowHeight
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

            // Draw all tokens left to right
            for (const auto &token : tokens) {
                if (token.first) {
                    // Emote rendering centralized in helper
                    renderEmote(display, nextX, lineY, rowHeight, token.second);
                } else {
                    // Text
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

        // Scrollbar
        if (messagesCount > _visibleRows) {
            int scrollHeight = display->getHeight() - listYOffset;
            int scrollTrackX = display->getWidth() - 6;
            display->drawRect(scrollTrackX, listYOffset, 4, scrollHeight);
            int barHeight = (scrollHeight * _visibleRows) / messagesCount;
            int scrollPos = listYOffset + (scrollHeight * topMsg) / messagesCount;
            display->fillRect(scrollTrackX, scrollPos, 4, barHeight);
        }
    }
}

// Return SNR limit based on modem preset
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

// Return Good/Fair/Bad label and set 1–5 bars based on SNR and RSSI
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
                graphics::BannerOverlayOptions opts;
                static char buf[128];

                const char *channelName = channels.getName(this->channel);
                const char *nodeName = getNodeName(this->incoming);

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

void CannedMessageModule::loadProtoForModule()
{
    if (nodeDB->loadProto(cannedMessagesConfigFile, meshtastic_CannedMessageModuleConfig_size,
                          sizeof(meshtastic_CannedMessageModuleConfig), &meshtastic_CannedMessageModuleConfig_msg,
                          &cannedMessageModuleConfig) != LoadFileResult::LOAD_SUCCESS) {
        installDefaultCannedMessageModuleConfig();
    }
}
/**
 * @brief Save the module config to file.
 *
 * @return true On success.
 * @return false On error.
 */
bool CannedMessageModule::saveProtoForModule()
{
    bool okay = true;

#ifdef FSCom
    spiLock->lock();
    FSCom.mkdir("/prefs");
    spiLock->unlock();
#endif

    okay &= nodeDB->saveProto(cannedMessagesConfigFile, meshtastic_CannedMessageModuleConfig_size,
                              &meshtastic_CannedMessageModuleConfig_msg, &cannedMessageModuleConfig);

    return okay;
}

/**
 * @brief Fill configuration with default values.
 */
void CannedMessageModule::installDefaultCannedMessageModuleConfig()
{
    strncpy(cannedMessageModuleConfig.messages, "Hi|Bye|Yes|No|Ok", sizeof(cannedMessageModuleConfig.messages));
}

/**
 * @brief An admin message arrived to AdminModule. We are asked whether we want to handle that.
 *
 * @param mp The mesh packet arrived.
 * @param request The AdminMessage request extracted from the packet.
 * @param response The prepared response
 * @return AdminMessageHandleResult HANDLED if message was handled
 *   HANDLED_WITH_RESULT if a result is also prepared.
 */
AdminMessageHandleResult CannedMessageModule::handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                                          meshtastic_AdminMessage *request,
                                                                          meshtastic_AdminMessage *response)
{
    AdminMessageHandleResult result;

    switch (request->which_payload_variant) {
    case meshtastic_AdminMessage_get_canned_message_module_messages_request_tag:
        LOG_DEBUG("Client getting radio canned messages");
        this->handleGetCannedMessageModuleMessages(mp, response);
        result = AdminMessageHandleResult::HANDLED_WITH_RESPONSE;
        break;

    case meshtastic_AdminMessage_set_canned_message_module_messages_tag:
        LOG_DEBUG("Client getting radio canned messages");
        this->handleSetCannedMessageModuleMessages(request->set_canned_message_module_messages);
        result = AdminMessageHandleResult::HANDLED;
        break;

    default:
        result = AdminMessageHandleResult::NOT_HANDLED;
    }

    return result;
}

void CannedMessageModule::handleGetCannedMessageModuleMessages(const meshtastic_MeshPacket &req,
                                                               meshtastic_AdminMessage *response)
{
    LOG_DEBUG("*** handleGetCannedMessageModuleMessages");
    if (req.decoded.want_response) {
        response->which_payload_variant = meshtastic_AdminMessage_get_canned_message_module_messages_response_tag;
        strncpy(response->get_canned_message_module_messages_response, cannedMessageModuleConfig.messages,
                sizeof(response->get_canned_message_module_messages_response));
    } // Don't send anything if not instructed to. Better than asserting.
}

void CannedMessageModule::handleSetCannedMessageModuleMessages(const char *from_msg)
{
    int changed = 0;

    if (*from_msg) {
        changed |= strcmp(cannedMessageModuleConfig.messages, from_msg);
        strncpy(cannedMessageModuleConfig.messages, from_msg, sizeof(cannedMessageModuleConfig.messages));
        LOG_DEBUG("*** from_msg.text:%s", from_msg);
    }

    if (changed) {
        this->saveProtoForModule();
        if (splitConfiguredMessages()) {
            moduleConfig.canned_message.enabled = true;
        }
    }
}

String CannedMessageModule::drawWithCursor(String text, int cursor)
{
    String result = text.substring(0, cursor) + "_" + text.substring(cursor);
    return result;
}

#endif
