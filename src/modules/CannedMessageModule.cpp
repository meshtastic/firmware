#include "configuration.h"
#if ARCH_PORTDUINO
#include "PortduinoGlue.h"
#endif
#if HAS_SCREEN
#include "CannedMessageModule.h"
#include "Channels.h"
#include "FSCommon.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h" // needed for button bypass
#include "SPILock.h"
#include "buzz.h"
#include "detect/ScanI2C.h"
#include "graphics/Screen.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/images.h"
#include "input/ScanAndSelect.h"
#include "main.h" // for cardkb_found
#include "mesh/generated/meshtastic/cannedmessages.pb.h"
#include "modules/AdminModule.h"
#include "modules/ExternalNotificationModule.h" // for buzzer control
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

extern ScanI2C::DeviceAddress cardkb_found;
extern bool graphics::isMuted;

static const char *cannedMessagesConfigFile = "/prefs/cannedConf.proto";

meshtastic_CannedMessageModuleConfig cannedMessageModuleConfig;

CannedMessageModule *cannedMessageModule;

CannedMessageModule::CannedMessageModule()
    : SinglePortModule("canned", meshtastic_PortNum_TEXT_MESSAGE_APP), concurrency::OSThread("CannedMessage")
{
    if (moduleConfig.canned_message.enabled || CANNED_MESSAGE_MODULE_ENABLE) {
        this->loadProtoForModule();
        if ((this->splitConfiguredMessages() <= 0) && (cardkb_found.address == 0x00) && !INPUTBROKER_MATRIX_TYPE &&
            !CANNED_MESSAGE_MODULE_ENABLE) {
            LOG_INFO("CannedMessageModule: No messages are configured. Module is disabled");
            this->runState = CANNED_MESSAGE_RUN_STATE_DISABLED;
            disable();
        } else {
            LOG_INFO("CannedMessageModule is enabled");
            this->inputObserver.observe(inputBroker);
        }
    } else {
        this->runState = CANNED_MESSAGE_RUN_STATE_DISABLED;
        disable();
    }
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

#if defined(USE_VIRTUAL_KEYBOARD)
    // Add a "Free Text" entry at the top if using a virtual keyboard
    String separator = canned_messages.length() ? "|" : "";
    canned_messages = "[---- Free Text ----]" + separator + canned_messages;
#endif

    // Copy all message parts into the buffer
    strncpy(this->messageStore, canned_messages.c_str(), sizeof(this->messageStore));

    // Temporary array to allow for insertion
    const char *tempMessages[CANNED_MESSAGE_MODULE_MESSAGE_MAX_COUNT + 3] = {0};
    int tempCount = 0;

    // First message always starts at buffer start
    tempMessages[tempCount++] = this->messageStore;
    int upTo = strlen(this->messageStore) - 1;

    // Walk buffer, splitting on '|'
    while (i < upTo) {
        if (this->messageStore[i] == '|') {
            this->messageStore[i] = '\0'; // End previous message
            if (tempCount >= CANNED_MESSAGE_MODULE_MESSAGE_MAX_COUNT)
                break;
            tempMessages[tempCount++] = (this->messageStore + i + 1);
        }
        i += 1;
    }

    // Insert "[Select Destination]" after Free Text if present, otherwise at the top
#if defined(USE_VIRTUAL_KEYBOARD)
    // Insert at position 1 (after Free Text)
    for (int j = tempCount; j > 1; j--)
        tempMessages[j] = tempMessages[j - 1];
    tempMessages[1] = "[Select Destination]";
    tempCount++;
#else
    // Insert at position 0 (top)
    for (int j = tempCount; j > 0; j--)
        tempMessages[j] = tempMessages[j - 1];
    tempMessages[0] = "[Select Destination]";
    tempCount++;
#endif

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
    if (display->getWidth() > 128) {
        if (this->dest == NODENUM_BROADCAST) {
            display->drawStringf(x, y, buffer, "To: Broadcast@%s", channels.getName(this->channel));
        } else {
            display->drawStringf(x, y, buffer, "To: %s@%s", getNodeName(this->dest), channels.getName(this->channel));
        }
    } else {
        if (this->dest == NODENUM_BROADCAST) {
            display->drawStringf(x, y, buffer, "To: Broadc@%.5s", channels.getName(this->channel));
        } else {
            display->drawStringf(x, y, buffer, "To: %.5s@%.5s", getNodeName(this->dest), channels.getName(this->channel));
        }
    }
}

void CannedMessageModule::resetSearch()
{
    LOG_INFO("Resetting search, restoring full destination list");

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
        if (!node || node->num == myNodeNum)
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

    // Sort by favorite, then last heard
    std::sort(this->filteredNodes.begin(), this->filteredNodes.end(), [](const NodeEntry &a, const NodeEntry &b) {
        if (a.node->is_favorite != b.node->is_favorite)
            return a.node->is_favorite > b.node->is_favorite;
        return a.lastHeard < b.lastHeard;
    });
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
    // Allow input only from configured source (hardware/software filter)
    if (!isInputSourceAllowed(event))
        return 0;

    // Global/system commands always processed (brightness, BT, GPS, shutdown, etc.)
    if (handleSystemCommandInput(event))
        return 1;

    // Tab key: Always allow switching between canned/destination screens
    if (event->kbchar == INPUT_BROKER_MSG_TAB && handleTabSwitch(event))
        return 1;

    // Matrix keypad: If matrix key, trigger action select for canned message
    if (event->inputEvent == static_cast<char>(MATRIXKEY)) {
        runState = CANNED_MESSAGE_RUN_STATE_ACTION_SELECT;
        payload = MATRIXKEY;
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
        return handleDestinationSelectionInput(event, isUp, isDown, isSelect); // All allowed input for this state

    // Free text input mode: Handles character input, cancel, backspace, select, etc.
    case CANNED_MESSAGE_RUN_STATE_FREETEXT:
        return handleFreeTextInput(event); // All allowed input for this state

    // If sending, block all input except global/system (handled above)
    case CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE:
        return 1;

    case CANNED_MESSAGE_RUN_STATE_INACTIVE:
        if (isSelect) {
            // When inactive, call the onebutton shortpress instead. Activate module only on up/down
            powerFSM.trigger(EVENT_PRESS);
            return 1; // Let caller know we handled it
        }
        // Let LEFT/RIGHT pass through so frame navigation works
        if (event->inputEvent == static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_LEFT) ||
            event->inputEvent == static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_RIGHT)) {
            break;
        }
        // Handle UP/DOWN: activate canned message list!
        if (event->inputEvent == static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_UP) ||
            event->inputEvent == static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_DOWN)) {
            // Always select the first real canned message on activation
            int firstRealMsgIdx = 0;
            for (int i = 0; i < messagesCount; ++i) {
                if (strcmp(messages[i], "[Select Destination]") != 0 && strcmp(messages[i], "[Exit]") != 0 &&
                    strcmp(messages[i], "[---- Free Text ----]") != 0) {
                    firstRealMsgIdx = i;
                    break;
                }
            }
            currentMessageIndex = firstRealMsgIdx;

            // This triggers the canned message list
            runState = CANNED_MESSAGE_RUN_STATE_ACTIVE;
            requestFocus();
            UIFrameEvent e;
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            notifyObservers(&e);
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

bool CannedMessageModule::isInputSourceAllowed(const InputEvent *event)
{
    return strlen(moduleConfig.canned_message.allow_input_source) == 0 ||
           strcasecmp(moduleConfig.canned_message.allow_input_source, event->source) == 0 ||
           strcasecmp(moduleConfig.canned_message.allow_input_source, "_any") == 0;
}

bool CannedMessageModule::isUpEvent(const InputEvent *event)
{
    return event->inputEvent == static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_UP);
}
bool CannedMessageModule::isDownEvent(const InputEvent *event)
{
    return event->inputEvent == static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_DOWN);
}
bool CannedMessageModule::isSelectEvent(const InputEvent *event)
{
    return event->inputEvent == static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT);
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

    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    notifyObservers(&e);
    screen->forceDisplay();
    return true;
}

int CannedMessageModule::handleDestinationSelectionInput(const InputEvent *event, bool isUp, bool isDown, bool isSelect)
{
    if (event->kbchar >= 32 && event->kbchar <= 126 && !isUp && !isDown &&
        event->inputEvent != static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_LEFT) &&
        event->inputEvent != static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_RIGHT) &&
        event->inputEvent != static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT)) {
        this->searchQuery += event->kbchar;
        needsUpdate = true;
        if ((millis() - lastFilterUpdate) > filterDebounceMs) {
            runOnce(); // update filter immediately
            lastFilterUpdate = millis();
        }
        return 0;
    }

    size_t numMeshNodes = filteredNodes.size();
    int totalEntries = numMeshNodes + activeChannelIndices.size();
    int columns = 1;
    int totalRows = totalEntries;
    int maxScrollIndex = std::max(0, totalRows - visibleRows);
    scrollIndex = clamp(scrollIndex, 0, maxScrollIndex);

    // Handle backspace
    if (event->inputEvent == static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_BACK)) {
        if (searchQuery.length() > 0) {
            searchQuery.remove(searchQuery.length() - 1);
            needsUpdate = true;
            runOnce();
        }
        if (searchQuery.length() == 0) {
            resetSearch();
            needsUpdate = false;
        }
        return 0;
    }

    // UP
    if (isUp && destIndex > 0) {
        destIndex--;
        if ((destIndex / columns) < scrollIndex)
            scrollIndex = destIndex / columns;
        else if ((destIndex / columns) >= (scrollIndex + visibleRows))
            scrollIndex = (destIndex / columns) - visibleRows + 1;

        screen->forceDisplay();
        return 0;
    }

    // DOWN
    if (isDown && destIndex + 1 < totalEntries) {
        destIndex++;
        if ((destIndex / columns) >= (scrollIndex + visibleRows))
            scrollIndex = (destIndex / columns) - visibleRows + 1;

        screen->forceDisplay();
        return 0;
    }

    // SELECT
    if (isSelect) {
        if (destIndex < static_cast<int>(activeChannelIndices.size())) {
            dest = NODENUM_BROADCAST;
            channel = activeChannelIndices[destIndex];
        } else {
            int nodeIndex = destIndex - static_cast<int>(activeChannelIndices.size());
            if (nodeIndex >= 0 && nodeIndex < static_cast<int>(filteredNodes.size())) {
                meshtastic_NodeInfoLite *selectedNode = filteredNodes[nodeIndex].node;
                if (selectedNode) {
                    dest = selectedNode->num;
                    channel = selectedNode->channel;
                }
            }
        }

        runState = returnToCannedList ? CANNED_MESSAGE_RUN_STATE_ACTIVE : CANNED_MESSAGE_RUN_STATE_FREETEXT;
        returnToCannedList = false;
        screen->forceDisplay();
        return 0;
    }

    // CANCEL
    if (event->inputEvent == static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_CANCEL)) {
        runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
        searchQuery = "";

        UIFrameEvent e;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
        screen->forceDisplay();
        return 0;
    }

    return 0;
}

bool CannedMessageModule::handleMessageSelectorInput(const InputEvent *event, bool isUp, bool isDown, bool isSelect)
{
    if (runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION)
        return false;

    // === Handle Cancel key: go inactive, clear UI state ===
    if (event->inputEvent == static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_CANCEL)) {
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

        // === [Select Destination] triggers destination selection UI ===
        if (strcmp(current, "[Select Destination]") == 0) {
            returnToCannedList = true;
            runState = CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION;
            destIndex = 0;
            scrollIndex = 0;
            updateDestinationSelectionList(); // Make sure list is fresh
            screen->forceDisplay();
            return true;
        }

        // === [Exit] returns to the main/inactive screen ===
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

        // === [Free Text] triggers the free text input (virtual keyboard) ===
#if defined(USE_VIRTUAL_KEYBOARD)
        if (currentMessageIndex == 0) {
            runState = CANNED_MESSAGE_RUN_STATE_FREETEXT;
            requestFocus();
            UIFrameEvent e;
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            notifyObservers(&e);
            return true;
        }
#endif

        // Normal canned message selection
        if (runState == CANNED_MESSAGE_RUN_STATE_INACTIVE || runState == CANNED_MESSAGE_RUN_STATE_DISABLED) {
            powerFSM.trigger(EVENT_PRESS);
        } else {
            payload = runState;
            runState = CANNED_MESSAGE_RUN_STATE_ACTION_SELECT;
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
        } else if (!keyTapped.isEmpty()) {
#ifndef RAK14014
            highlight = keyTapped[0];
#endif
            payload = shift ? keyTapped[0] : std::tolower(keyTapped[0]);
            shift = false;
            valid = true;
        }

        if (valid) {
            lastTouchMillis = millis();
            return true; // STOP: We handled a VKB touch
        }
    }
#endif // USE_VIRTUAL_KEYBOARD

    // ---- All hardware keys fall through to here (CardKB, physical, etc.) ----

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
    if (event->inputEvent == static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_BACK)) {
        payload = 0x08;
        lastTouchMillis = millis();
        runOnce();
        return true;
    }

    // Move cursor left
    if (event->inputEvent == static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_LEFT)) {
        payload = INPUT_BROKER_MSG_LEFT;
        lastTouchMillis = millis();
        runOnce();
        return true;
    }
    // Move cursor right
    if (event->inputEvent == static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_RIGHT)) {
        payload = INPUT_BROKER_MSG_RIGHT;
        lastTouchMillis = millis();
        runOnce();
        return true;
    }

    // Cancel (dismiss freetext screen)
    if (event->inputEvent == static_cast<char>(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_CANCEL)) {
        runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
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

bool CannedMessageModule::handleSystemCommandInput(const InputEvent *event)
{
    // Only respond to "ANYKEY" events for system keys
    if (event->inputEvent != static_cast<char>(ANYKEY))
        return false;

    // Block ALL input if an alert banner is active // TODO: Make an accessor function
    if (screen && screen->isOverlayBannerShowing()) {
        return true;
    }

    // System commands (all others fall through to return false)
    switch (event->kbchar) {
    // Fn key symbols
    case INPUT_BROKER_MSG_FN_SYMBOL_ON:
        if (screen)
            screen->setFunctionSymbol("Fn");
        return true;
    case INPUT_BROKER_MSG_FN_SYMBOL_OFF:
        if (screen)
            screen->removeFunctionSymbol("Fn");
        return true;
    // Brightness
    case INPUT_BROKER_MSG_BRIGHTNESS_UP:
        if (screen)
            screen->increaseBrightness();
        LOG_DEBUG("Increase Screen Brightness");
        return true;
    case INPUT_BROKER_MSG_BRIGHTNESS_DOWN:
        if (screen)
            screen->decreaseBrightness();
        LOG_DEBUG("Decrease Screen Brightness");
        return true;
    // Mute
    case INPUT_BROKER_MSG_MUTE_TOGGLE:
        if (moduleConfig.external_notification.enabled && externalNotificationModule) {
            bool isMuted = externalNotificationModule->getMute();
            externalNotificationModule->setMute(!isMuted);
            graphics::isMuted = !isMuted;
            if (!isMuted)
                externalNotificationModule->stopNow();
            if (screen)
                screen->showOverlayBanner(isMuted ? "Notifications\nEnabled" : "Notifications\nDisabled", 3000);
        }
        return true;
    // Bluetooth
    case INPUT_BROKER_MSG_BLUETOOTH_TOGGLE:
        config.bluetooth.enabled = !config.bluetooth.enabled;
        LOG_INFO("User toggled Bluetooth");
        nodeDB->saveToDisk();
#if defined(ARDUINO_ARCH_NRF52)
        if (!config.bluetooth.enabled) {
            disableBluetooth();
            if (screen)
                screen->showOverlayBanner("Bluetooth OFF\nRebooting", 3000);
            rebootAtMsec = millis() + DEFAULT_REBOOT_SECONDS * 2000;
        } else {
            if (screen)
                screen->showOverlayBanner("Bluetooth ON\nRebooting", 3000);
            rebootAtMsec = millis() + DEFAULT_REBOOT_SECONDS * 1000;
        }
#else
        if (!config.bluetooth.enabled) {
            disableBluetooth();
            if (screen)
                screen->showOverlayBanner("Bluetooth OFF", 3000);
        } else {
            if (screen)
                screen->showOverlayBanner("Bluetooth ON\nRebooting", 3000);
            rebootAtMsec = millis() + DEFAULT_REBOOT_SECONDS * 1000;
        }
#endif
        return true;
    // GPS
    case INPUT_BROKER_MSG_GPS_TOGGLE:
#if !MESHTASTIC_EXCLUDE_GPS
        if (gps) {
            gps->toggleGpsMode();
            const char *msg =
                (config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED) ? "GPS Enabled" : "GPS Disabled";
            if (screen) {
                screen->forceDisplay();
                screen->showOverlayBanner(msg, 3000);
            }
        }
#endif
        return true;
    // Mesh ping
    case INPUT_BROKER_MSG_SEND_PING:
        service->refreshLocalMeshNode();
        if (service->trySendPosition(NODENUM_BROADCAST, true)) {
            if (screen)
                screen->showOverlayBanner("Position\nUpdate Sent", 3000);
        } else {
            if (screen)
                screen->showOverlayBanner("Node Info\nUpdate Sent", 3000);
        }
        return true;
    // Power control
    case INPUT_BROKER_MSG_SHUTDOWN:
        if (screen)
            screen->showOverlayBanner("Shutting down...");
        nodeDB->saveToDisk();
        shutdownAtMsec = millis() + DEFAULT_SHUTDOWN_SECONDS * 1000;
        runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
        return true;
    case INPUT_BROKER_MSG_REBOOT:
        if (screen)
            screen->showOverlayBanner("Rebooting...", 0);
        nodeDB->saveToDisk();
        rebootAtMsec = millis() + DEFAULT_REBOOT_SECONDS * 1000;
        runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
        return true;
    case INPUT_BROKER_MSG_DISMISS_FRAME:
        runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
        if (screen)
            screen->dismissCurrentFrame();
        return true;
    // Not a system command, let other handlers process it
    default:
        return false;
    }
}

void CannedMessageModule::sendText(NodeNum dest, ChannelIndex channel, const char *message, bool wantReplies)
{
    // === Prepare packet ===
    meshtastic_MeshPacket *p = allocDataPacket();
    p->to = dest;
    p->channel = channel;
    p->want_ack = true;

    // Save destination for ACK/NACK UI fallback
    this->lastSentNode = dest;
    this->incoming = dest;

    // Copy message payload
    p->decoded.payload.size = strlen(message);
    memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);

    // Optionally add bell character
    if (moduleConfig.canned_message.send_bell && p->decoded.payload.size < meshtastic_Constants_DATA_PAYLOAD_LEN) {
        p->decoded.payload.bytes[p->decoded.payload.size++] = 7;  // Bell
        p->decoded.payload.bytes[p->decoded.payload.size] = '\0'; // Null-terminate
    }

    // Mark as waiting for ACK to trigger ACK/NACK screen
    this->waitingForAck = true;

    // Log outgoing message
    LOG_INFO("Send message id=%d, dest=%x, msg=%.*s", p->id, p->to, p->decoded.payload.size, p->decoded.payload.bytes);

    // Send to mesh and phone (even if no phone connected, to track ACKs)
    service->sendToMesh(p, RX_SRC_LOCAL, true);

    // === Simulate local message to clear unread UI ===
    if (screen) {
        meshtastic_MeshPacket simulatedPacket = {};
        simulatedPacket.from = 0; // Local device
        screen->handleTextMessage(&simulatedPacket);
    }
    playComboTune();
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
    if (((!moduleConfig.canned_message.enabled) && !CANNED_MESSAGE_MODULE_ENABLE) ||
        (this->runState == CANNED_MESSAGE_RUN_STATE_DISABLED) || (this->runState == CANNED_MESSAGE_RUN_STATE_INACTIVE)) {
        temporaryMessage = "";
        return INT32_MAX;
    }

    UIFrameEvent e;
    if ((this->runState == CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE) ||
        (this->runState == CANNED_MESSAGE_RUN_STATE_ACK_NACK_RECEIVED) ||
        (this->runState == CANNED_MESSAGE_RUN_STATE_MESSAGE_SELECTION)) {
        this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
        temporaryMessage = "";
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        this->currentMessageIndex = -1;
        this->freetext = "";
        this->cursor = 0;
#if !defined(T_WATCH_S3) && !defined(RAK14014) && !defined(SENSECAP_INDICATOR)
        int destSelect = 0;
#endif
        this->notifyObservers(&e);
    } else if (((this->runState == CANNED_MESSAGE_RUN_STATE_ACTIVE) || (this->runState == CANNED_MESSAGE_RUN_STATE_FREETEXT)) &&
               !Throttle::isWithinTimespanMs(this->lastTouchMillis, INACTIVATE_AFTER_MS)) {
        // Reset module on inactivity
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        this->currentMessageIndex = -1;
        this->freetext = "";
        this->cursor = 0;
#if !defined(T_WATCH_S3) && !defined(RAK14014) && !defined(USE_VIRTUAL_KEYBOARD)
        int destSelect = 0;
#endif
        this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
        this->notifyObservers(&e);
    } else if (this->runState == CANNED_MESSAGE_RUN_STATE_ACTION_SELECT) {
        if (this->payload == CANNED_MESSAGE_RUN_STATE_FREETEXT) {
            if (this->freetext.length() > 0) {
                sendText(this->dest, this->channel, this->freetext.c_str(), true);
                this->runState = CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE;
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
                    powerFSM.trigger(EVENT_PRESS);
                    return INT32_MAX;
                } else {
                    sendText(this->dest, this->channel, this->messages[this->currentMessageIndex], true);
                }
                this->runState = CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE;
            } else {
                this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
            }
        }
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        this->currentMessageIndex = -1;
        this->freetext = "";
        this->cursor = 0;
        this->notifyObservers(&e);
        return 2000;
    }
    // Always highlight the first real canned message when entering the message list
    else if ((this->runState != CANNED_MESSAGE_RUN_STATE_FREETEXT) && (this->currentMessageIndex == -1)) {
        int firstRealMsgIdx = 0;
        for (int i = 0; i < this->messagesCount; ++i) {
            if (strcmp(this->messages[i], "[Select Destination]") != 0 && strcmp(this->messages[i], "[Exit]") != 0 &&
                strcmp(this->messages[i], "[---- Free Text ----]") != 0) {
                firstRealMsgIdx = i;
                break;
            }
        }
        this->currentMessageIndex = firstRealMsgIdx;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        this->runState = CANNED_MESSAGE_RUN_STATE_ACTIVE;
    } else if (this->runState == CANNED_MESSAGE_RUN_STATE_ACTION_UP) {
        if (this->messagesCount > 0) {
            this->currentMessageIndex = getPrevIndex();
            this->freetext = "";
            this->cursor = 0;
            this->runState = CANNED_MESSAGE_RUN_STATE_ACTIVE;
            LOG_DEBUG("MOVE UP (%d):%s", this->currentMessageIndex, this->getCurrentMessage());
        }
    } else if (this->runState == CANNED_MESSAGE_RUN_STATE_ACTION_DOWN) {
        if (this->messagesCount > 0) {
            this->currentMessageIndex = this->getNextIndex();
            this->freetext = "";
            this->cursor = 0;
            this->runState = CANNED_MESSAGE_RUN_STATE_ACTIVE;
            LOG_DEBUG("MOVE DOWN (%d):%s", this->currentMessageIndex, this->getCurrentMessage());
        }
    } else if (this->runState == CANNED_MESSAGE_RUN_STATE_FREETEXT || this->runState == CANNED_MESSAGE_RUN_STATE_ACTIVE) {
        switch (this->payload) {
        case INPUT_BROKER_MSG_LEFT:
            if (this->runState == CANNED_MESSAGE_RUN_STATE_FREETEXT && this->cursor > 0) {
                this->cursor--;
            }
            break;
        case INPUT_BROKER_MSG_RIGHT:
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
                if (this->freetext.length() > 0 && this->highlight == 0x00) {
                    if (this->cursor == this->freetext.length()) {
                        this->freetext = this->freetext.substring(0, this->freetext.length() - 1);
                    } else {
                        this->freetext = this->freetext.substring(0, this->cursor - 1) +
                                         this->freetext.substring(this->cursor, this->freetext.length());
                    }
                    this->cursor--;
                }
                break;
            case INPUT_BROKER_MSG_TAB: // Tab key: handled by input handler
                return 0;
            case INPUT_BROKER_MSG_LEFT:
            case INPUT_BROKER_MSG_RIGHT:
                break;
            default:
                if (this->highlight != 0x00)
                    break;
                if (this->cursor == this->freetext.length()) {
                    this->freetext += this->payload;
                } else {
                    this->freetext =
                        this->freetext.substring(0, this->cursor) + this->payload + this->freetext.substring(this->cursor);
                }
                this->cursor += 1;
                uint16_t maxChars = meshtastic_Constants_DATA_PAYLOAD_LEN - (moduleConfig.canned_message.send_bell ? 1 : 0);
                if (this->freetext.length() > maxChars) {
                    this->cursor = maxChars;
                    this->freetext = this->freetext.substring(0, maxChars);
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
    if (!moduleConfig.canned_message.enabled && !CANNED_MESSAGE_MODULE_ENABLE) {
        return false;
    }

    // If using "scan and select" input, don't draw the module frame just to say "disabled"
    // The scanAndSelectInput class will draw its own temporary alert for user, when the input button is pressed
    else if (scanAndSelectInput != nullptr && !hasMessages())
        return false;

    return (currentMessageIndex != -1) || (this->runState != CANNED_MESSAGE_RUN_STATE_INACTIVE);
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
void CannedMessageModule::showTemporaryMessage(const String &message)
{
    temporaryMessage = message;
    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET; // We want to change the list of frames shown on-screen
    notifyObservers(&e);
    runState = CANNED_MESSAGE_RUN_STATE_MESSAGE_SELECTION;
    // run this loop again in 2 seconds, next iteration will clear the display
    setIntervalFromNow(2000);
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

    // === Header ===
    int titleY = 2;
    String titleText = "Select Destination";
    titleText += searchQuery.length() > 0 ? " [" + searchQuery + "]" : " [ ]";
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(display->getWidth() / 2, titleY, titleText);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    // === List Items ===
    int rowYOffset = titleY + (FONT_HEIGHT_SMALL - 4);
    int numActiveChannels = this->activeChannelIndices.size();
    int totalEntries = numActiveChannels + this->filteredNodes.size();
    int columns = 1;
    this->visibleRows = (display->getHeight() - (titleY + FONT_HEIGHT_SMALL)) / (FONT_HEIGHT_SMALL - 4);
    if (this->visibleRows < 1)
        this->visibleRows = 1;

    // === Clamp scrolling ===
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
            snprintf(entryText, sizeof(entryText), "@%s", channels.getName(channelIndex));
        }
        // Then Draw Nodes
        else {
            int nodeIndex = itemIndex - numActiveChannels;
            if (nodeIndex >= 0 && nodeIndex < static_cast<int>(this->filteredNodes.size())) {
                meshtastic_NodeInfoLite *node = this->filteredNodes[nodeIndex].node;
                if (node) {
                    if (node->is_favorite) {
                        snprintf(entryText, sizeof(entryText), "* %s", node->user.long_name);
                    } else {
                        snprintf(entryText, sizeof(entryText), "%s", node->user.long_name);
                    }
                }
            }
        }

        if (strlen(entryText) == 0 || strcmp(entryText, "Unknown") == 0)
            strcpy(entryText, "?");

        // === Highlight background (if selected) ===
        if (itemIndex == destIndex) {
            int scrollPadding = 8; // Reserve space for scrollbar
            display->fillRect(0, yOffset + 2, display->getWidth() - scrollPadding, FONT_HEIGHT_SMALL - 5);
            display->setColor(BLACK);
        }

        // === Draw entry text ===
        display->drawString(xOffset + 2, yOffset, entryText);
        display->setColor(WHITE);

        // === Draw key icon (after highlight) ===
        if (itemIndex >= numActiveChannels) {
            int nodeIndex = itemIndex - numActiveChannels;
            if (nodeIndex >= 0 && nodeIndex < static_cast<int>(this->filteredNodes.size())) {
                meshtastic_NodeInfoLite *node = this->filteredNodes[nodeIndex].node;
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

void CannedMessageModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    this->displayHeight = display->getHeight(); // Store display height for later use
    char buffer[50];
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    // === Draw temporary message if available ===
    if (temporaryMessage.length() != 0) {
        requestFocus(); // Tell Screen::setFrames to move to our module's frame
        LOG_DEBUG("Draw temporary message: %s", temporaryMessage.c_str());
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->setFont(FONT_MEDIUM);
        display->drawString(display->getWidth() / 2 + x, 0 + y + 12, temporaryMessage);
        return;
    }

    // === Destination Selection ===
    if (this->runState == CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION) {
        drawDestinationSelectionScreen(display, state, x, y);
        return;
    }

    // === ACK/NACK Screen ===
    if (this->runState == CANNED_MESSAGE_RUN_STATE_ACK_NACK_RECEIVED) {
        requestFocus();
        EINK_ADD_FRAMEFLAG(display, COSMETIC);
        display->setTextAlignment(TEXT_ALIGN_CENTER);

#ifdef USE_EINK
        display->setFont(FONT_SMALL);
        int yOffset = y + 10;
#else
        display->setFont(FONT_MEDIUM);
        int yOffset = y + 10;
#endif

        // --- Delivery Status Message ---
        if (this->ack) {
            if (this->lastSentNode == NODENUM_BROADCAST) {
                snprintf(buffer, sizeof(buffer), "Broadcast Sent to\n%s", channels.getName(this->channel));
            } else if (this->lastAckHopLimit > this->lastAckHopStart) {
                snprintf(buffer, sizeof(buffer), "Delivered (%d hops)\nto %s", this->lastAckHopLimit - this->lastAckHopStart,
                         getNodeName(this->incoming));
            } else {
                snprintf(buffer, sizeof(buffer), "Delivered\nto %s", getNodeName(this->incoming));
            }
        } else {
            snprintf(buffer, sizeof(buffer), "Delivery failed\nto %s", getNodeName(this->incoming));
        }

        // Draw delivery message and compute y-offset after text height
        int lineCount = 1;
        for (const char *ptr = buffer; *ptr; ptr++) {
            if (*ptr == '\n')
                lineCount++;
        }

        display->drawString(display->getWidth() / 2 + x, yOffset, buffer);
        yOffset += lineCount * FONT_HEIGHT_MEDIUM; // only 1 line gap, no extra padding

#ifndef USE_EINK
        // --- SNR + RSSI Compact Line ---
        if (this->ack) {
            display->setFont(FONT_SMALL);
            snprintf(buffer, sizeof(buffer), "SNR: %.1f dB   RSSI: %d", this->lastRxSnr, this->lastRxRssi);
            display->drawString(display->getWidth() / 2 + x, yOffset, buffer);
        }
#endif

        return;
    }

    // === Sending Screen ===
    if (this->runState == CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE) {
        EINK_ADD_FRAMEFLAG(display, COSMETIC);
        requestFocus();
#ifdef USE_EINK
        display->setFont(FONT_SMALL);
#else
        display->setFont(FONT_MEDIUM);
#endif
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(display->getWidth() / 2 + x, 0 + y + 12, "Sending...");
        return;
    }

    // === Disabled Screen ===
    if (this->runState == CANNED_MESSAGE_RUN_STATE_DISABLED) {
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->setFont(FONT_SMALL);
        display->drawString(10 + x, 0 + y + FONT_HEIGHT_SMALL, "Canned Message\nModule disabled.");
        return;
    }

    // === Free Text Input Screen ===
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

        // --- Draw node/channel header at the top ---
        drawHeader(display, x, y, buffer);

        // --- Char count right-aligned ---
        if (runState != CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION) {
            uint16_t charsLeft =
                meshtastic_Constants_DATA_PAYLOAD_LEN - this->freetext.length() - (moduleConfig.canned_message.send_bell ? 1 : 0);
            snprintf(buffer, sizeof(buffer), "%d left", charsLeft);
            display->drawString(x + display->getWidth() - display->getStringWidth(buffer), y + 0, buffer);
        }

        // --- Draw Free Text input, shifted down ---
        display->setColor(WHITE);
        display->drawStringMaxWidth(0 + x, 0 + y + FONT_HEIGHT_SMALL, x + display->getWidth(),
                                    drawWithCursor(this->freetext, this->cursor));
#endif
        return;
    }

    // === Canned Messages List ===
    if (this->messagesCount > 0) {
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->setFont(FONT_SMALL);

        const int rowSpacing = FONT_HEIGHT_SMALL - 4;

        // Draw header (To: ...)
        drawHeader(display, x, y, buffer);

        // Shift message list upward by 3 pixels to reduce spacing between header and first message
        const int listYOffset = y + FONT_HEIGHT_SMALL - 3;
        const int visibleRows = (display->getHeight() - listYOffset) / rowSpacing;

        int topMsg =
            (messagesCount > visibleRows && currentMessageIndex >= visibleRows - 1) ? currentMessageIndex - visibleRows + 2 : 0;

        for (int i = 0; i < std::min(messagesCount, visibleRows); i++) {
            int lineY = listYOffset + rowSpacing * i;
            const char *msg = getMessageByIndex(topMsg + i);

            if ((topMsg + i) == currentMessageIndex) {
#ifdef USE_EINK
                display->drawString(x + 0, lineY, ">");
                display->drawString(x + 12, lineY, msg);
#else
                int scrollPadding = 8;
                display->fillRect(x + 0, lineY + 2, display->getWidth() - scrollPadding, FONT_HEIGHT_SMALL - 5);
                display->setColor(BLACK);
                display->drawString(x + 2, lineY, msg);
                display->setColor(WHITE);
#endif
            } else {
                display->drawString(x + 0, lineY, msg);
            }
        }

        // Scrollbar
        if (messagesCount > visibleRows) {
            int scrollHeight = display->getHeight() - listYOffset;
            int scrollTrackX = display->getWidth() - 6;
            display->drawRect(scrollTrackX, listYOffset, 4, scrollHeight);
            int barHeight = (scrollHeight * visibleRows) / messagesCount;
            int scrollPos = listYOffset + (scrollHeight * topMsg) / messagesCount;
            display->fillRect(scrollTrackX, scrollPos, 4, barHeight);
        }
    }
}

ProcessMessage CannedMessageModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (mp.decoded.portnum == meshtastic_PortNum_ROUTING_APP && waitingForAck) {
        if (mp.decoded.request_id != 0) {
            // Trigger screen refresh for ACK/NACK feedback
            UIFrameEvent e;
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            requestFocus();
            this->runState = CANNED_MESSAGE_RUN_STATE_ACK_NACK_RECEIVED;

            // Decode the routing response
            meshtastic_Routing decoded = meshtastic_Routing_init_default;
            pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, meshtastic_Routing_fields, &decoded);

            // Track hop metadata
            this->lastAckWasRelayed = (mp.hop_limit != mp.hop_start);
            this->lastAckHopStart = mp.hop_start;
            this->lastAckHopLimit = mp.hop_limit;

            // Determine ACK status
            bool isAck = (decoded.error_reason == meshtastic_Routing_Error_NONE);
            bool isFromDest = (mp.from == this->lastSentNode);
            bool isBroadcast = (this->lastSentNode == NODENUM_BROADCAST);

            // Identify the responding node
            if (isBroadcast && mp.from != nodeDB->getNodeNum()) {
                this->incoming = mp.from; // Relayed by another node
            } else {
                this->incoming = this->lastSentNode; // Direct reply
            }

            // Final ACK confirmation logic
            this->ack = isAck && (isBroadcast || isFromDest);

            waitingForAck = false;
            this->notifyObservers(&e);
            setIntervalFromNow(3000); // Time to show ACK/NACK screen
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
    memset(cannedMessageModuleConfig.messages, 0, sizeof(cannedMessageModuleConfig.messages));
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
    }
}

String CannedMessageModule::drawWithCursor(String text, int cursor)
{
    String result = text.substring(0, cursor) + "_" + text.substring(cursor);
    return result;
}

bool CannedMessageModule::isInterceptingAndFocused()
{
    return this->interceptingKeyboardInput();
}

#endif