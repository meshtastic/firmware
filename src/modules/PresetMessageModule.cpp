#include "configuration.h"
#include "PresetMessageModule.h"

#if defined(ELECROW_ThinkNode_M8)

PresetMessageModule *presetmessagemodule;

PresetMessageModule::PresetMessageModule() : SinglePortModule("Preset", meshtastic_PortNum_TEXT_MESSAGE_APP), concurrency::OSThread("PresetMessage")
{
    if ((this->ConfiguredPresetMessages() <= 0)  && !PRESET_MESSAGE_MODULE_ENABLE) 
    {
        LOG_INFO("PresetMessageModule: No messages are configured. Module is disabled");

        this->runState = PRESET_MESSAGE_RUN_STATE_DISABLED;
        disable();
    } 
    else 
    {
        LOG_INFO("PresetMessageModule is enabled");
        this->inputObserver.observe(inputBroker);
    }
}

void PresetMessageModule::LaunchWithDestination(NodeNum newDest, uint8_t newChannel)
{
    // Use the requested destination, unless it's "broadcast" and we have a previous node/channel
    if (newDest == NODENUM_BROADCAST && lastDestSet) {
        newDest = lastDest;
        newChannel = lastChannel;
    }
    dest = newDest;
    channel = newChannel;
    lastDest = dest;
    lastChannel = channel;
    lastDestSet = true;
    int selectDestination = 0;
    for (int i = 0; i < priorityCount; ++i) {
        if (strcmp(messages_priority[i], "[Select Destination]") == 0) {
            selectDestination = i;
            break;
        }
    }
    currentpriorityIndex = selectDestination;
    runState = PRESET_MESSAGE_RUN_STATE_ACTIVE;
    requestFocus();
    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    notifyObservers(&e);
    setIntervalFromNow(PRESET_INACTIVATE_AFTER_MS);
}

void PresetMessageModule::LaunchRepeatDestination()
{
    if (!lastDestSet) {
        LaunchWithDestination(NODENUM_BROADCAST, 0);
    } else {
        LaunchWithDestination(lastDest, lastChannel);
    }
}

void PresetMessageModule::clean_PresetMessageModule_state()
{
    currentpriorityIndex = -1;
    currentMessageIndex = -1;
    runState = PRESET_MESSAGE_RUN_STATE_INACTIVE;
}

bool PresetMessageModule::shouldDraw()
{
    return (currentpriorityIndex != -1) || (this->runState != PRESET_MESSAGE_RUN_STATE_INACTIVE);
}

bool PresetMessageModule::interceptingKeyboardInput()
{
    switch (runState) 
    {
    case PRESET_MESSAGE_RUN_STATE_DISABLED:
    case PRESET_MESSAGE_RUN_STATE_INACTIVE:
        return false;
    default:
        return true;
    }
}

bool PresetMessageModule::hasKeyForNode(const meshtastic_NodeInfoLite *node)
{
    return node && node->has_user && node->user.public_key.size > 0;
}

void PresetMessageModule::drawHeader(OLEDDisplay *display, int16_t x, int16_t y, char *buffer)
{
    if (graphics::isHighResolution) {
        if (this->dest == NODENUM_BROADCAST) {
            display->drawStringf(x, y, buffer, "To: Broadcast@%s", channels.getName(this->channel));
        } else {
            display->drawStringf(x, y, buffer, "To: %s", getNodeName(this->dest));
        }
    } else {
        if (this->dest == NODENUM_BROADCAST) {
            display->drawStringf(x, y, buffer, "To: Broadc@%.5s", channels.getName(this->channel));
        } else {
            display->drawStringf(x, y, buffer, "To: %s", getNodeName(this->dest));
        }
    }
}

const char *PresetMessageModule::getNodeName(NodeNum node)
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

void PresetMessageModule::updateDestinationSelectionList()
{
    static size_t lastNumMeshNodes = 0;

    size_t numMeshNodes = nodeDB->getNumMeshNodes();
    bool nodesChanged = (numMeshNodes != lastNumMeshNodes);
    lastNumMeshNodes = numMeshNodes;

    // Early exit if nothing changed
    if (!nodesChanged)
        return;

    this->filteredNodes.clear();
    this->activeChannelIndices.clear();

    NodeNum myNodeNum = nodeDB->getNodeNum();

    // Preallocate space to reduce reallocation
    this->filteredNodes.reserve(numMeshNodes);

    for (size_t i = 0; i < numMeshNodes; ++i) 
    {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
        if (!node || node->num == myNodeNum)
            continue;

        const String &nodeName = node->user.long_name;
        this->filteredNodes.push_back({node, sinceLastSeen(node)});

    }

    // Populate active channels
    std::vector<String> seenChannels;
    seenChannels.reserve(channels.getNumChannels());
    for (uint8_t i = 0; i < channels.getNumChannels(); ++i) 
    {
        String name = channels.getName(i);
        if (name.length() > 0 && std::find(seenChannels.begin(), seenChannels.end(), name) == seenChannels.end()) 
        {
            this->activeChannelIndices.push_back(i);
            seenChannels.push_back(name);
        }
    }

    destIndex = 0;   // Highlight the first entry
    if (nodesChanged && runState == PRESET_MESSAGE_RUN_STATE_DESTINATION_SELECTION) 
    {
        LOG_INFO("Nodes changed, forcing UI refresh.");
        screen->forceDisplay();
    }
}

int PresetMessageModule::ConfiguredPresetMessages()
{
    priorityCount = 0;
    highestCount = 0;
    highCount = 0;
    middleCount = 0;
    lowCount = 0;
    generalCount = 0;
    messages_priority[priorityCount++] = "[Select Destination]";
    messages_priority[priorityCount++] = "[Highest]";
    messages_priority[priorityCount++] = "[High]";
    messages_priority[priorityCount++] = "[Middle]";
    messages_priority[priorityCount++] = "[Low]";
    messages_priority[priorityCount++] = "[General]";
    messages_priority[priorityCount++] = "[Exit]";

    Highest_messages[highestCount++] = "[SOS! Need Emergency Rescue!]";
    Highest_messages[highestCount++] = "[Injured, need medical help!]";
    Highest_messages[highestCount++] = "[Lost, need directions!]";
    Highest_messages[highestCount++] = "[In danger, be cautious!]";
    Highest_messages[highestCount++] = "[Accident occurred, request backup!]";
    Highest_messages[highestCount++] = "[Exit]";

    High_messages[highCount++] = "[OK]";
    High_messages[highCount++] = "[This is my current location]";
    High_messages[highCount++] = "[Arrived at destination]";
    High_messages[highCount++] = "[Returning]";
    High_messages[highCount++] = "[On schedule]";
    High_messages[highCount++] = "[Running behind, but OK]";
    High_messages[highCount++] = "[Request your position]";
    High_messages[highCount++] = "[Stopped moving]";
    High_messages[highCount++] = "[On the move]";
    High_messages[highCount++] = "[Exit]";

    Middle_messages[middleCount++] = "[Regroup on me]";
    Middle_messages[middleCount++] = "[Continue forward]";
    Middle_messages[middleCount++] = "[Request rendezvous]";
    Middle_messages[middleCount++] = "[Need medical supplies]";
    Middle_messages[middleCount++] = "[Hold position]";
    Middle_messages[middleCount++] = "[Speed up]";
    Middle_messages[middleCount++] = "[Need to rest]";
    Middle_messages[middleCount++] = "[Need water/food]";
    Middle_messages[middleCount++] = "[Gear failure]";
    Middle_messages[middleCount++] = "[Exit]";

    Low_messages[lowCount++] = "[Weather deteriorating]";
    Low_messages[lowCount++] = "[Obstacle ahead]";
    Low_messages[lowCount++] = "[Dangerous terrain]";
    Low_messages[lowCount++] = "[Wildlife spotted]";
    Low_messages[lowCount++] = "[We got separated]";
    Low_messages[lowCount++] = "[Exit]";

    General_messages[generalCount++] = "[Received / Copy that]";
    General_messages[generalCount++] = "[Affirmative / Yes]";
    General_messages[generalCount++] = "[Negative / No]";
    General_messages[generalCount++] = "[Unable to comply]";
    General_messages[generalCount++] = "[Will contact later]";
    General_messages[generalCount++] = "[Comms check 1-2-3]";
    General_messages[generalCount++] = "[Low battery]";
    General_messages[generalCount++] = "[Exit]";

    return priorityCount;
}

bool PresetMessageModule::isUpEvent(const InputEvent *event)
{
    return ((runState == PRESET_MESSAGE_RUN_STATE_ACTIVE || runState == PRESET_MESSAGE_RUN_STATE_DESTINATION_SELECTION || runState == PRESET_MESSAGE_RUN_STATE_MESSAGE_SELECTION) && (event->inputEvent == INPUT_BROKER_LEFT));
}

bool PresetMessageModule::isDownEvent(const InputEvent *event)
{
    return ((runState == PRESET_MESSAGE_RUN_STATE_ACTIVE || runState == PRESET_MESSAGE_RUN_STATE_DESTINATION_SELECTION || runState == PRESET_MESSAGE_RUN_STATE_MESSAGE_SELECTION) && (event->inputEvent == INPUT_BROKER_RIGHT));
}

bool PresetMessageModule::isSelectEvent(const InputEvent *event)
{
    return event->inputEvent == INPUT_BROKER_SELECT;
}

int PresetMessageModule::handleDestinationSelectionInput(const InputEvent *event, bool isUp, bool isDown, bool isSelect)
{
    if (runState == PRESET_MESSAGE_RUN_STATE_ACTIVE)
        return false;

    if (runState == PRESET_MESSAGE_RUN_STATE_MESSAGE_SELECTION)
        return false;

    if (runState == PRESET_MESSAGE_RUN_STATE_INACTIVE || runState == PRESET_MESSAGE_RUN_STATE_DISABLED)
        return false;

    size_t numMeshNodes = filteredNodes.size();
    int totalEntries = numMeshNodes + activeChannelIndices.size();
    if (isSelect) 
    {
        if (destIndex < static_cast<int>(activeChannelIndices.size())) 
        {
            dest = NODENUM_BROADCAST;
            channel = activeChannelIndices[destIndex];
            lastDest = dest;
            lastChannel = channel;
            lastDestSet = true;
        } 
        else 
        {
            int nodeIndex = destIndex - static_cast<int>(activeChannelIndices.size());
            if (nodeIndex >= 0 && nodeIndex < static_cast<int>(filteredNodes.size())) 
            {
                const meshtastic_NodeInfoLite *selectedNode = filteredNodes[nodeIndex].node;
                if (selectedNode) 
                {
                    dest = selectedNode->num;
                    channel = selectedNode->channel;
                    lastDest = dest;
                    lastChannel = channel;
                    lastDestSet = true;
                }
            }
        }
        runState = PRESET_MESSAGE_RUN_STATE_ACTIVE;
        screen->forceDisplay(true);
        setIntervalFromNow(PRESET_INACTIVATE_AFTER_MS);
        return 1;
    }
    else if (isUp) 
    {
        if (destIndex > 0) 
            destIndex--;
        else if (totalEntries > 0) 
            destIndex = totalEntries - 1;

        screen->forceDisplay(true);
        return 1;
    }
    else if (isDown) 
    {
        if (destIndex + 1 < totalEntries)
            destIndex++;
        else if (totalEntries > 0) 
            destIndex = 0;

        screen->forceDisplay(true);
        return 1;
    }
    return 0;
}

bool PresetMessageModule::handlePrioritySelectorInput(const InputEvent *event, bool isUp, bool isDown, bool isSelect)
{
    if (runState == PRESET_MESSAGE_RUN_STATE_DESTINATION_SELECTION)
        return false;

    if (runState == PRESET_MESSAGE_RUN_STATE_MESSAGE_SELECTION)
        return false;

    if (runState == PRESET_MESSAGE_RUN_STATE_INACTIVE || runState == PRESET_MESSAGE_RUN_STATE_DISABLED)
        return false;

    UIFrameEvent e;
    bool state = false;
    if (isSelect) 
    {
        const char *current = messages_priority[currentpriorityIndex];
        /*=== [Select Destination] triggers destination selection UI ===*/ 
        if (strcmp(current, "[Select Destination]") == 0) 
        {
            runState = PRESET_MESSAGE_RUN_STATE_DESTINATION_SELECTION;
            destIndex = 0;
            updateDestinationSelectionList(); // Make sure list is fresh
            requestFocus();
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            this->notifyObservers(&e);
            screen->forceDisplay();
            return true;
        }
        else if (strcmp(current, "[Highest]") == 0) 
        {
            runState = PRESET_MESSAGE_RUN_STATE_MESSAGE_SELECTION;
            messageCount = highestCount;
            if (highestCount <= PRESET_MESSAGE_MODULE_MESSAGES_MAX_COUNT) 
            {
                std::copy(Highest_messages, Highest_messages + highestCount, messages_array);
            } 
            else 
            {
                LOG_ERROR("Highest_messages count exceeds maximum allowed");
                messageCount = PRESET_MESSAGE_MODULE_MESSAGES_MAX_COUNT;
                std::copy(Highest_messages, Highest_messages + PRESET_MESSAGE_MODULE_MESSAGES_MAX_COUNT, messages_array);
            }
            currentMessageIndex = 0;
            requestFocus();
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            this->notifyObservers(&e);
            screen->forceDisplay();
            return true;
        }
        else if (strcmp(current, "[High]") == 0) 
        {
            runState = PRESET_MESSAGE_RUN_STATE_MESSAGE_SELECTION;
            messageCount = highCount;
            if (highCount <= PRESET_MESSAGE_MODULE_MESSAGES_MAX_COUNT) 
            {
                std::copy(High_messages, High_messages + highCount, messages_array);
            } 
            else 
            {
                LOG_ERROR("Highest_messages count exceeds maximum allowed");
                messageCount = PRESET_MESSAGE_MODULE_MESSAGES_MAX_COUNT;
                std::copy(High_messages, High_messages + PRESET_MESSAGE_MODULE_MESSAGES_MAX_COUNT, messages_array);
            }
            currentMessageIndex = 0;
            requestFocus();
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            this->notifyObservers(&e);
            screen->forceDisplay();
            return true;
        }
        else if (strcmp(current, "[Middle]") == 0) 
        {
            runState = PRESET_MESSAGE_RUN_STATE_MESSAGE_SELECTION;
            messageCount = middleCount;
            if (middleCount <= PRESET_MESSAGE_MODULE_MESSAGES_MAX_COUNT) 
            {
                std::copy(Middle_messages, Middle_messages + middleCount, messages_array);
            } 
            else 
            {
                LOG_ERROR("Highest_messages count exceeds maximum allowed");
                messageCount = PRESET_MESSAGE_MODULE_MESSAGES_MAX_COUNT;
                std::copy(Middle_messages, Middle_messages + PRESET_MESSAGE_MODULE_MESSAGES_MAX_COUNT, messages_array);
            }
            currentMessageIndex = 0;
            requestFocus();
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            this->notifyObservers(&e);
            screen->forceDisplay();
            return true;
        }
        else if (strcmp(current, "[Low]") == 0) 
        {
            runState = PRESET_MESSAGE_RUN_STATE_MESSAGE_SELECTION;
            messageCount = lowCount;
            if (lowCount <= PRESET_MESSAGE_MODULE_MESSAGES_MAX_COUNT) 
            {
                std::copy(Low_messages, Low_messages + lowCount, messages_array);
            } 
            else 
            {
                LOG_ERROR("Highest_messages count exceeds maximum allowed");
                messageCount = PRESET_MESSAGE_MODULE_MESSAGES_MAX_COUNT;
                std::copy(Low_messages, Low_messages + PRESET_MESSAGE_MODULE_MESSAGES_MAX_COUNT, messages_array);
            }
            currentMessageIndex = 0;
            requestFocus();
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            this->notifyObservers(&e);
            screen->forceDisplay();
            return true;
        }
        else if (strcmp(current, "[General]") == 0) 
        {
            runState = PRESET_MESSAGE_RUN_STATE_MESSAGE_SELECTION;
            messageCount = generalCount;
            if (generalCount <= PRESET_MESSAGE_MODULE_MESSAGES_MAX_COUNT) 
            {
                std::copy(General_messages, General_messages + generalCount, messages_array);
            } 
            else 
            {
                LOG_ERROR("Highest_messages count exceeds maximum allowed");
                messageCount = PRESET_MESSAGE_MODULE_MESSAGES_MAX_COUNT;
                std::copy(General_messages, General_messages + PRESET_MESSAGE_MODULE_MESSAGES_MAX_COUNT, messages_array);
            }
            currentMessageIndex = 0;
            requestFocus();
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            this->notifyObservers(&e);
            screen->forceDisplay();
            return true;
        }
        else if (strcmp(current, "[Exit]") == 0) // === [Exit] returns to the main/inactive screen ===
        {
            runState = PRESET_MESSAGE_RUN_STATE_INACTIVE;
            currentpriorityIndex = -1;
            currentMessageIndex = -1;
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            notifyObservers(&e);
            screen->forceDisplay();
            return true;
        }   
    }
    // Handle up/down navigation
    else if (isUp && priorityCount > 0) 
    {
        this->currentpriorityIndex = getPrevPriorityIndex();
        state = true;
    } 
    else if (isDown && priorityCount > 0) 
    {
        this->currentpriorityIndex = getNextPriorityIndex();
        state = true;
    } 
    if(state)
    {
        requestFocus();
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        this->notifyObservers(&e);
        screen->forceDisplay();
    }
    return state;
}

bool PresetMessageModule::handleMessageSelectorInput(const InputEvent *event, bool isUp, bool isDown, bool isSelect)
{
    if (runState == PRESET_MESSAGE_RUN_STATE_ACTIVE)
        return false;

    if (runState == PRESET_MESSAGE_RUN_STATE_DESTINATION_SELECTION)
        return false;

    if (runState == PRESET_MESSAGE_RUN_STATE_INACTIVE || runState == PRESET_MESSAGE_RUN_STATE_DISABLED)
        return false;

    bool state = false;
    UIFrameEvent e;
    if (isSelect) 
    {
        const char *current = messages_array[currentMessageIndex];
        /*=== [Exit] returns to the priority screen ===*/ 
        if (strcmp(current, "[Exit]") == 0) 
        {
            runState = PRESET_MESSAGE_RUN_STATE_ACTIVE;
            currentMessageIndex = -1;
            requestFocus();
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            this->notifyObservers(&e);
            screen->forceDisplay();
            setIntervalFromNow(PRESET_INACTIVATE_AFTER_MS);
            return true;
        }
        else 
        {
            runState = PRESET_MESSAGE_RUN_STATE_ACTION_SELECT;
            state = true;
        }
        
    }
    // Handle up/down navigation
    else if (isUp && messageCount > 0) 
    {
        this->currentMessageIndex = getPrevMessagesIndex();
        state = true;
    } 
    else if (isDown && messageCount > 0) 
    {
        this->currentMessageIndex = getNextMessagesIndex();
        state = true;
    } 
    if (state) 
    {
        
        if (runState == PRESET_MESSAGE_RUN_STATE_ACTION_SELECT)
            setIntervalFromNow(0);
        else
        {
            requestFocus();
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            this->notifyObservers(&e);
            screen->forceDisplay();
        }
    }
    return state;
}

int PresetMessageModule::handleInputEvent(const InputEvent *event)
{
    // Block ALL input if an alert banner is active
    if (screen && screen->isOverlayBannerShowing()) 
    {
        return 0;
    }
    bool isUp = isUpEvent(event);
    bool isDown = isDownEvent(event);
    bool isSelect = isSelectEvent(event);
    LOG_DEBUG("event = %d",event->inputEvent);

    this->LastOperationTime = millis();

    switch (runState) 
    {
    // Node/Channel destination selection mode: Handles character search, arrows, select, cancel, backspace
    case PRESET_MESSAGE_RUN_STATE_DESTINATION_SELECTION:
        if (handleDestinationSelectionInput(event, isUp, isDown, isSelect))
            return 1;
        return 0; // prevent fall-through to selector input

    case PRESET_MESSAGE_RUN_STATE_MESSAGE_SELECTION:
        if (handleMessageSelectorInput(event, isUp, isDown, isSelect))
            return 1;
        return 0; 

    // If sending, block all input except global/system (handled above)
    case PRESET_MESSAGE_RUN_STATE_SENDING_ACTIVE:
        return 1;

    case PRESET_MESSAGE_RUN_STATE_INACTIVE:
        if (isSelect) 
        {
            return 0; // Main button press no longer runs through powerFSM
        }
        // Let LEFT/RIGHT pass through so frame navigation works
        if (event->inputEvent == INPUT_BROKER_LEFT || event->inputEvent == INPUT_BROKER_RIGHT) {
            break;
        }
        if (event->inputEvent == INPUT_BROKER_SELECT_LONG) 
        {
            LOG_DEBUG("activate preset message list");
            LaunchWithDestination(NODENUM_BROADCAST);
            return 1;
        }

    default:
        break;
    }

    if (handlePrioritySelectorInput(event, isUp, isDown, isSelect))// If no state handler above processed the event, let the priority selector try to handle it
        return 1;

    return 0; //Default: event not handled by preset message system, allow others to process
}

void PresetMessageModule::sendText(NodeNum dest, ChannelIndex channel, const char *message, bool wantReplies)
{
    lastDest = dest;
    lastChannel = channel;
    lastDestSet = true;
    String cleanMessage = String(message);
    if (cleanMessage.length() >= 2 && cleanMessage.startsWith("[") && cleanMessage.endsWith("]")) 
        cleanMessage = cleanMessage.substring(1, cleanMessage.length() - 1);


    // === Prepare packet ===
    meshtastic_MeshPacket *p = allocDataPacket();
    p->to = dest;
    p->channel = channel;
    p->want_ack = true;

    // Save destination for ACK/NACK UI fallback
    this->lastSentNode = dest;
    this->incoming = dest;

    // Copy message payload
    p->decoded.payload.size = cleanMessage.length();
    memcpy(p->decoded.payload.bytes, cleanMessage.c_str(), p->decoded.payload.size);

    // Optionally add bell character
    if (moduleConfig.canned_message.send_bell && p->decoded.payload.size < meshtastic_Constants_DATA_PAYLOAD_LEN) 
    {
        p->decoded.payload.bytes[p->decoded.payload.size++] = 7;  // Bell
        p->decoded.payload.bytes[p->decoded.payload.size] = '\0'; // Null-terminate
    }

    // Mark as waiting for ACK to trigger ACK/NACK screen
    this->waitingForAck = true;

    // Log outgoing message
    LOG_INFO("Send message id=%u, dest=%x, msg=%.*s", p->id, p->to, p->decoded.payload.size, p->decoded.payload.bytes);

    if (p->to != 0xffffffff) {
        LOG_INFO("Proactively adding %x as favorite node", p->to);
        nodeDB->set_favorite(true, p->to);
        screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
    }

    // Send to mesh and phone (even if no phone connected, to track ACKs)
    service->sendToMesh(p, RX_SRC_LOCAL, true);
    playComboTune();
}

int PresetMessageModule::getNextPriorityIndex()
{
    if (this->currentpriorityIndex >= (this->priorityCount - 1)) 
        return 0;
    else 
        return this->currentpriorityIndex + 1;
}

int PresetMessageModule::getPrevPriorityIndex()
{
    if (this->currentpriorityIndex <= 0)
        return this->priorityCount - 1;
    else
        return this->currentpriorityIndex - 1;
}

int PresetMessageModule::getNextMessagesIndex()
{
    if (this->currentMessageIndex >= (this->messageCount - 1)) 
        return 0;
    else 
        return this->currentMessageIndex + 1;
}

int PresetMessageModule::getPrevMessagesIndex()
{
    if (this->currentMessageIndex <= 0)
        return this->messageCount - 1;
    else
        return this->currentMessageIndex - 1;
}

const char *PresetMessageModule::getPriorityByIndex(int index)
{
    return (index >= 0 && index < this->priorityCount) ? this->messages_priority[index] : "";
}

const char *PresetMessageModule::getMessageByIndex(int index)
{
    return (index >= 0 && index < this->messageCount) ? this->messages_array[index] : "";
}

void PresetMessageModule::drawDestinationSelectionScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    requestFocus();
    display->setColor(WHITE);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    /*==== Line spacing configuration ====*/ 
    const int EXTRA_ROW_SPACING = 8;
    const int baseRowSpacing = FONT_HEIGHT_SMALL + EXTRA_ROW_SPACING;

    int topEntry;
    std::vector<int> estimatedHeights;
    std::vector<int> actualHeights;
    std::vector<int> rowStartYs;
    int _visibleRows;

    /*=== Header ===*/ 
    int titleY = 2;
    String titleText = "Select Destination";
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(display->getWidth() / 2, titleY, titleText);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    /*=== List Items ===*/ 
    const int listYOffset = titleY + FONT_HEIGHT_SMALL;
    int availableHeight = display->getHeight() - listYOffset;
    int numActiveChannels = this->activeChannelIndices.size();
    int totalEntries = numActiveChannels + this->filteredNodes.size();

    // Pre-calculate estimated heights for all entries
    for (int i = 0; i < totalEntries; i++) 
    {
        char entryText[64] = "";
        
        if (i < numActiveChannels) 
        {
            uint8_t channelIndex = this->activeChannelIndices[i];
            snprintf(entryText, sizeof(entryText), "@%s", channels.getName(channelIndex));
        }
        else 
        {
            int nodeIndex = i - numActiveChannels;
            if (nodeIndex >= 0 && nodeIndex < static_cast<int>(this->filteredNodes.size())) 
            {
                meshtastic_NodeInfoLite *node = this->filteredNodes[nodeIndex].node;
                if (node) 
                {
                    if (node->is_favorite) 
                        snprintf(entryText, sizeof(entryText), "* %s", node->user.long_name);
                    else 
                        snprintf(entryText, sizeof(entryText), "%s", node->user.long_name);
                }
            }
        }

        if (strlen(entryText) == 0 || strcmp(entryText, "Unknown") == 0)
            strcpy(entryText, "?");

        int scrollPadding = 15;
        int maxLineWidth = display->getWidth() - scrollPadding - x;
        std::vector<String> lines;
        String currentLine = "";
        
        String textStr = String(entryText);
        int startPos = 0;
        
        for (unsigned int j = 0; j <= textStr.length(); j++) 
        {
            if (j == textStr.length() || textStr[j] == ' ') 
            {
                String word = textStr.substring(startPos, j);
                String testLine = currentLine;
                
                if (testLine.length() > 0) 
                    testLine += " " + word;
                else 
                    testLine = word;
                
                if (display->getStringWidth(testLine) > maxLineWidth && currentLine.length() > 0) 
                {
                    lines.push_back(currentLine);
                    currentLine = word;
                } 
                else 
                    currentLine = testLine;
            
                startPos = j + 1;
            }
        }
        
        if (currentLine.length() > 0) 
        {
            lines.push_back(currentLine);
        }
        
        int rowHeight = baseRowSpacing * std::max(1, (int)lines.size());
        estimatedHeights.push_back(rowHeight);
    }

    /*=== Rolling logic ===*/ 
    int totalHeight = 0;
    for (int i = 0; i < totalEntries; i++) 
    {
        totalHeight += estimatedHeights[i];
    }
    int avgHeight = totalHeight / std::max(1, totalEntries);
    int estimatedVisibleRows = availableHeight / avgHeight;
    
    estimatedVisibleRows = std::max(1, std::min(estimatedVisibleRows, totalEntries));
    
    if (totalEntries <= estimatedVisibleRows) 
    {
        topEntry = 0;
        _visibleRows = totalEntries;
    } 
    else 
    {
        int halfWindow = estimatedVisibleRows / 2;
        topEntry = destIndex - halfWindow;
        
        if (topEntry < 0) 
            topEntry = 0;
        else if (topEntry + estimatedVisibleRows > totalEntries) 
            topEntry = totalEntries - estimatedVisibleRows;
        
        _visibleRows = estimatedVisibleRows;
    }

    int countRows = 0;
    int yCursor = listYOffset;
    actualHeights.clear();
    rowStartYs.clear();

    for (int i = topEntry; i < totalEntries && countRows < _visibleRows; i++) 
    {
        if (yCursor + estimatedHeights[i] <= listYOffset + availableHeight) 
        {
            countRows++;
            actualHeights.push_back(estimatedHeights[i]);
            rowStartYs.push_back(yCursor);
            yCursor += estimatedHeights[i];
        }
        else 
        {
            break;
        }
    }

    _visibleRows = countRows;

    /*=== Draw all rows ===*/ 
    yCursor = listYOffset;
    
    for (int vis = 0; vis < countRows; vis++) 
    {
        int itemIndex = topEntry + vis;
        int lineY = rowStartYs[vis];
        
        char entryText[64] = "";
        bool isChannel = (itemIndex < numActiveChannels);

        if (isChannel) 
        {
            uint8_t channelIndex = this->activeChannelIndices[itemIndex];
            snprintf(entryText, sizeof(entryText), "@%s", channels.getName(channelIndex));
        }
        else 
        {
            int nodeIndex = itemIndex - numActiveChannels;
            if (nodeIndex >= 0 && nodeIndex < static_cast<int>(this->filteredNodes.size())) 
            {
                meshtastic_NodeInfoLite *node = this->filteredNodes[nodeIndex].node;
                if (node) 
                {
                    if (node->is_favorite) 
                        snprintf(entryText, sizeof(entryText), "* %s", node->user.long_name);
                    else 
                        snprintf(entryText, sizeof(entryText), "%s", node->user.long_name);
                }
            }
        }

        if (strlen(entryText) == 0 || strcmp(entryText, "Unknown") == 0)
            strcpy(entryText, "?");

        bool _highlight = (itemIndex == destIndex);
        
        int scrollPadding = 15;
        int maxLineWidth = display->getWidth() - scrollPadding - x;
        
        std::vector<String> lines;
        String currentLine = "";
        
        String textStr = String(entryText);
        int startPos = 0;
        
        for (unsigned int j = 0; j <= textStr.length(); j++) 
        {
            if (j == textStr.length() || textStr[j] == ' ') 
            {
                String word = textStr.substring(startPos, j);
                String testLine = currentLine;
                
                if (testLine.length() > 0) 
                    testLine += " " + word;
                else 
                    testLine = word;
                
                if (display->getStringWidth(testLine) > maxLineWidth && currentLine.length() > 0) 
                {
                    lines.push_back(currentLine);
                    currentLine = word;
                } 
                else 
                    currentLine = testLine;
            
                startPos = j + 1;
            }
        }
        
        if (currentLine.length() > 0) 
        {
            lines.push_back(currentLine);
        }
        
        int rowHeight = baseRowSpacing * std::max(1, (int)lines.size());
        
        if (_highlight) 
        {
            display->fillRect(x, lineY, display->getWidth() - scrollPadding, rowHeight);
            display->setColor(BLACK);
        }
        else 
        {
            display->setColor(WHITE);
        }
        
        int currentY = lineY;
        for (size_t i = 0; i < lines.size(); i++) 
        {
            String line = lines[i];
            int textYOffset = (baseRowSpacing - FONT_HEIGHT_SMALL) / 2;
            display->drawString(x, currentY + textYOffset, line);
            currentY += baseRowSpacing;
        }
        
        display->setColor(WHITE);

        // Draw key icon for nodes with public keys
        if (!isChannel) 
        {
            int nodeIndex = itemIndex - numActiveChannels;
            if (nodeIndex >= 0 && nodeIndex < static_cast<int>(this->filteredNodes.size())) 
            {
                const meshtastic_NodeInfoLite *node = this->filteredNodes[nodeIndex].node;
                if (node && hasKeyForNode(node)) 
                {
                    int iconX = display->getWidth() - key_symbol_width - scrollPadding;
                    int iconY = lineY + (rowHeight - key_symbol_height) / 2;

                    if (_highlight) 
                        display->setColor(INVERSE);
                    else 
                        display->setColor(WHITE);
                    display->drawXbm(iconX, iconY, key_symbol_width, key_symbol_height, key_symbol);
                    display->setColor(WHITE);
                }
            }
        }
        
        yCursor += rowHeight;
    }

    /*=== Scroll bar calculation ===*/
    int borderPadding = 2; 
    int barWidth = 4;   

    int scrollTrackX = display->getWidth() - 6;
    int scrollTrackTop = listYOffset;
    int scrollTrackBottom = yCursor;
    int scrollTrackHeight = scrollTrackBottom - scrollTrackTop;

    int totalContentHeight = 0;
    for (int i = 0; i < totalEntries; i++) 
    {
        totalContentHeight += estimatedHeights[i];
    }

    int barHeight = baseRowSpacing;

    display->setColor(WHITE);
    display->drawRect(scrollTrackX - borderPadding, scrollTrackTop - borderPadding, barWidth + 2 * borderPadding, scrollTrackHeight + 2 * borderPadding);

    int selectedRowStartY = -1;
    int selectedRowHeight = 0;
    
    for (int vis = 0; vis < countRows; vis++) 
    {
        int itemIndex = topEntry + vis;
        if (itemIndex == destIndex) 
        {
            selectedRowStartY = rowStartYs[vis];
            selectedRowHeight = actualHeights[vis];
            break;
        }
    }
    
    int scrollPos;
    
    if (selectedRowStartY != -1) 
    {
        int selectedRowCenterY = selectedRowStartY + selectedRowHeight / 2;
        scrollPos = selectedRowCenterY - barHeight / 2;
    }
    else 
    {
        int contentBeforeSelected = 0;
        for (int i = 0; i < destIndex; i++) 
        {
            contentBeforeSelected += estimatedHeights[i];
        }
        
        float scrollRatio = (float)contentBeforeSelected / (float)(totalContentHeight - availableHeight);
        scrollRatio = std::max(0.0f, std::min(1.0f, scrollRatio));
        
        int availableScrollSpace = scrollTrackHeight - barHeight;
        scrollPos = scrollTrackTop + (int)(availableScrollSpace * scrollRatio);
    }
    
    scrollPos = std::max(scrollTrackTop, std::min(scrollPos, scrollTrackBottom - barHeight));
    
    if (scrollPos >= scrollTrackTop && scrollPos + barHeight <= scrollTrackBottom)
        display->fillRect(scrollTrackX, scrollPos, barWidth, barHeight);

}

void PresetMessageModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    this->displayHeight = display->getHeight(); // Store display height for later use
    char buffer[50];
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    /*=== Destination Selection ===*/ 
    if (this->runState == PRESET_MESSAGE_RUN_STATE_DESTINATION_SELECTION) 
    {
        drawDestinationSelectionScreen(display, state, x, y);
        return;
    }

    /*=== ACK/NACK Screen ===*/ 
    else if (this->runState == PRESET_MESSAGE_RUN_STATE_ACK_NACK_RECEIVED) 
    {
        EINK_ADD_FRAMEFLAG(display, COSMETIC);
        display->setTextAlignment(TEXT_ALIGN_CENTER);

#ifdef USE_EINK
        display->setFont(FONT_SMALL);
        int yOffset = y + 10;
#else
        display->setFont(FONT_MEDIUM);
        int yOffset = y + 10;
#endif

        /*--- Delivery Status Message ---*/ 
        if (this->ack) 
        {
            if (this->lastSentNode == NODENUM_BROADCAST) 
            {
                snprintf(buffer, sizeof(buffer), "Broadcast Sent to\n%s", channels.getName(this->channel));
            } 
            else if (this->lastAckHopLimit > this->lastAckHopStart) 
            {
                snprintf(buffer, sizeof(buffer), "Delivered (%d hops)\nto %s", this->lastAckHopLimit - this->lastAckHopStart, getNodeName(this->incoming));
            } 
            else 
            {
                snprintf(buffer, sizeof(buffer), "Delivered\nto %s", getNodeName(this->incoming));
            }
        } 
        else 
        {
            snprintf(buffer, sizeof(buffer), "Delivery failed\nto %s", getNodeName(this->incoming));
        }

        // Draw delivery message and compute y-offset after text height
        int lineCount = 1;
        for (const char *ptr = buffer; *ptr; ptr++) 
        {
            if (*ptr == '\n')
                lineCount++;
        }

        display->drawString(display->getWidth() / 2 + x, yOffset, buffer);
        yOffset += lineCount * FONT_HEIGHT_MEDIUM; // only 1 line gap, no extra padding

#ifndef USE_EINK
        // --- SNR + RSSI Compact Line ---
        if (this->ack) 
        {
            display->setFont(FONT_SMALL);
            snprintf(buffer, sizeof(buffer), "SNR: %.1f dB   RSSI: %d", this->lastRxSnr, this->lastRxRssi);
            display->drawString(display->getWidth() / 2 + x, yOffset, buffer);
        }
#endif

        return;
    }

    /*=== Sending Screen ===*/
    else if (this->runState == PRESET_MESSAGE_RUN_STATE_SENDING_ACTIVE) 
    {
        EINK_ADD_FRAMEFLAG(display, COSMETIC);
#ifdef USE_EINK
        display->setFont(FONT_SMALL);
#else
        display->setFont(FONT_MEDIUM);
#endif
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(display->getWidth() / 2 + x, 0 + y + 12, "Sending...");
        return;
    }

    /*=== Disabled Screen ===*/ 
    else if (this->runState == PRESET_MESSAGE_RUN_STATE_DISABLED) 
    {
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->setFont(FONT_SMALL);
        display->drawString(10 + x, 0 + y + FONT_HEIGHT_SMALL, "Preset Message\nModule disabled.");
        return;
    }
    
     /*=== Preset messages List ===*/ 
    else if(((this->runState == PRESET_MESSAGE_RUN_STATE_MESSAGE_SELECTION) || (this->runState == PRESET_MESSAGE_RUN_STATE_ACTION_SELECT)) && (this->messageCount > 0))
    {
        display->setColor(WHITE);
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->setFont(FONT_SMALL);

        /*==== Line spacing configuration ====*/ 
        const int EXTRA_ROW_SPACING = 8;
        const int baseRowSpacing = FONT_HEIGHT_SMALL + EXTRA_ROW_SPACING;

        int topMsg;
        std::vector<int> estimatedHeights;
        std::vector<int> actualHeights;
        std::vector<int> rowStartYs;
        int _visibleRows;

        drawHeader(display, x, y, buffer);

        const int listYOffset = y + FONT_HEIGHT_SMALL;
        int availableHeight = display->getHeight() - listYOffset;

        // Pre-calculate estimated heights for all messages
        for (int i = 0; i < messageCount; i++) 
        {
            const char *msg = getMessageByIndex(i);
        
            int scrollPadding = 15;
            int maxLineWidth = display->getWidth() - scrollPadding - x;
            std::vector<String> lines;
            String currentLine = "";
            
            String messageStr = String(msg);
            int startPos = 0;
            
            for (unsigned int j = 0; j <= messageStr.length(); j++) 
            {
                if (j == messageStr.length() || messageStr[j] == ' ') 
                {
                    String word = messageStr.substring(startPos, j);
                    String testLine = currentLine;
                    
                    if (testLine.length() > 0) 
                        testLine += " " + word;
                    else 
                        testLine = word;
                    
                    if (display->getStringWidth(testLine) > maxLineWidth && currentLine.length() > 0) 
                    {
                        lines.push_back(currentLine);
                        currentLine = word;
                    } 
                    else 
                        currentLine = testLine;
                
                    startPos = j + 1;
                }
            }
            
            if (currentLine.length() > 0) 
            {
                lines.push_back(currentLine);
            }
            
            int maxEmoteHeight = 0;
            for (int j = 0; j < graphics::numEmotes; j++) 
            {
                const char *label = graphics::emotes[j].label;
                if (!label || !*label)
                    continue;
                const char *search = msg;
                while ((search = strstr(search, label))) 
                {
                    if (graphics::emotes[j].height > maxEmoteHeight)
                        maxEmoteHeight = graphics::emotes[j].height;
                    search += strlen(label);
                }
            }
            
            int textHeight = baseRowSpacing * std::max(1, (int)lines.size());
            int emoteHeight = maxEmoteHeight + 2 + EXTRA_ROW_SPACING;
            int rowHeight = std::max(textHeight, emoteHeight);
            
            estimatedHeights.push_back(rowHeight);
        }

        /*=== Rolling logic ===*/ 
        int totalHeight = 0;
        for (int i = 0; i < messageCount; i++) 
        {
            totalHeight += estimatedHeights[i];
        }
        int avgHeight = totalHeight / messageCount;
        int estimatedVisibleRows = availableHeight / avgHeight;
        
        estimatedVisibleRows = std::max(1, std::min(estimatedVisibleRows, messageCount));
        
        if (messageCount <= estimatedVisibleRows) 
        {
            topMsg = 0;
            _visibleRows = messageCount;
        } 
        else 
        {
            int halfWindow = estimatedVisibleRows / 2;
            topMsg = currentMessageIndex - halfWindow;
            
            if (topMsg < 0) 
                topMsg = 0;
            else if (topMsg + estimatedVisibleRows > messageCount) 
                topMsg = messageCount - estimatedVisibleRows;
            
            _visibleRows = estimatedVisibleRows;
        }

        int countRows = 0;
        int yCursor = listYOffset;
        actualHeights.clear();
        rowStartYs.clear();

        for (int i = topMsg; i < messageCount && countRows < _visibleRows; i++) 
        {
            if (yCursor + estimatedHeights[i] <= listYOffset + availableHeight) 
            {
                countRows++;
                actualHeights.push_back(estimatedHeights[i]);
                rowStartYs.push_back(yCursor);
                yCursor += estimatedHeights[i];
            }
            else 
            {
                break;
            }
        }

        _visibleRows = countRows;
        for (int vis = 0; vis < countRows; vis++) 
        {
            int msgIdx = topMsg + vis;
            int lineY = rowStartYs[vis];
            
            const char *msg = getMessageByIndex(msgIdx);
            bool _highlight = (msgIdx == currentMessageIndex); 
            
            int scrollPadding = 15;
            int maxLineWidth = display->getWidth() - scrollPadding - x;
            
            std::vector<String> lines;
            String currentLine = "";
            
            String messageStr = String(msg);
            int startPos = 0;
            
            for (unsigned int j = 0; j <= messageStr.length(); j++) 
            {
                if (j == messageStr.length() || messageStr[j] == ' ') 
                {
                    String word = messageStr.substring(startPos, j);
                    String testLine = currentLine;
                    
                    if (testLine.length() > 0) 
                        testLine += " " + word;
                    else 
                        testLine = word;
                    
                    if (display->getStringWidth(testLine) > maxLineWidth && currentLine.length() > 0) 
                    {
                        lines.push_back(currentLine);
                        currentLine = word;
                    } 
                    else 
                        currentLine = testLine;
                
                    startPos = j + 1;
                }
            }
            
            if (currentLine.length() > 0) 
            {
                lines.push_back(currentLine);
            }
            
            int maxEmoteHeight = 0;
            for (int j = 0; j < graphics::numEmotes; j++) 
            {
                const char *label = graphics::emotes[j].label;
                if (!label || !*label)
                    continue;
                const char *search = msg;
                while ((search = strstr(search, label))) 
                {
                    if (graphics::emotes[j].height > maxEmoteHeight)
                        maxEmoteHeight = graphics::emotes[j].height;
                    search += strlen(label);
                }
            }
            
            int textHeight = baseRowSpacing * std::max(1, (int)lines.size());
            int emoteHeight = maxEmoteHeight + 2 + EXTRA_ROW_SPACING;
            int rowHeight = std::max(textHeight, emoteHeight);
            
            if (_highlight) 
            {
                display->fillRect(x, lineY, display->getWidth() - scrollPadding, rowHeight);
                display->setColor(BLACK);
            }
            else 
            {
                display->setColor(WHITE);
            }
            
            int currentY = lineY;
            int nextX = x;
            
            if (lines.size() > 1) 
            {
                for (size_t i = 0; i < lines.size(); i++) 
                {
                    String line = lines[i];
                    int textYOffset = (baseRowSpacing - FONT_HEIGHT_SMALL) / 2;
                    display->drawString(x, currentY + textYOffset, line);
                    currentY += baseRowSpacing;
                }
            }
            else 
            {
                std::vector<std::pair<bool, String>> tokens;
                int pos = 0;
                int msgLen = strlen(msg);
                while (pos < msgLen) 
                {
                    const graphics::Emote *foundEmote = nullptr;
                    int foundLen = 0;
                    for (int j = 0; j < graphics::numEmotes; j++) 
                    {
                        const char *label = graphics::emotes[j].label;
                        int labelLen = strlen(label);
                        if (labelLen == 0)
                            continue;
                        if (strncmp(msg + pos, label, labelLen) == 0)
                        {
                            if (!foundEmote || labelLen > foundLen) 
                            {
                                foundEmote = &graphics::emotes[j];
                                foundLen = labelLen;
                            }
                        }
                    }
                    if (foundEmote)
                    {
                        tokens.emplace_back(true, String(foundEmote->label));
                        pos += foundLen;
                    } 
                    else 
                    {
                        int nextEmote = msgLen;
                        for (int j = 0; j < graphics::numEmotes; j++) {
                            const char *label = graphics::emotes[j].label;
                            if (label[0] == 0)
                                continue;
                            const char *found = strstr(msg + pos, label);
                            if (found && (found - msg) < nextEmote) {
                                nextEmote = found - msg;
                            }
                        }
                        int textLen = (nextEmote > pos) ? (nextEmote - pos) : (msgLen - pos);
                        if (textLen > 0) 
                        {
                            tokens.emplace_back(false, String(msg + pos).substring(0, textLen));
                            pos += textLen;
                        } 
                        else 
                        {
                            break;
                        }
                    }
                }
                
                int textYOffset = (rowHeight - FONT_HEIGHT_SMALL) / 2;
                
                for (const auto &token : tokens) 
                {
                    if (token.first) 
                    {
                        const graphics::Emote *emote = nullptr;
                        for (int j = 0; j < graphics::numEmotes; j++) 
                        {
                            if (token.second == graphics::emotes[j].label) 
                            {
                                emote = &graphics::emotes[j];
                                break;
                            }
                        }
                        if (emote) 
                        {
                            int emoteYOffset = (rowHeight - emote->height) / 2;
                            display->drawXbm(nextX, lineY + emoteYOffset, emote->width, emote->height, emote->bitmap);
                            nextX += emote->width + 2;
                        }
                    } 
                    else 
                    {
                        display->drawString(nextX, lineY + textYOffset, token.second);
                        nextX += display->getStringWidth(token.second);
                    }
                }
            }
            
            display->setColor(WHITE);
        }

        /*=== Scroll bar calculation ===*/
        int borderPadding = 2; 
        int barWidth = 4;   

        int scrollTrackX = display->getWidth() - 6;
        int scrollTrackTop = listYOffset;
        int scrollTrackBottom = yCursor;
        int scrollTrackHeight = scrollTrackBottom - scrollTrackTop;

        int totalContentHeight = 0;
        for (int i = 0; i < messageCount; i++) 
        {
            totalContentHeight += estimatedHeights[i];
        }

        int barHeight = baseRowSpacing;

        display->setColor(WHITE);
        display->drawRect(scrollTrackX - borderPadding, scrollTrackTop - borderPadding, barWidth + 2 * borderPadding, scrollTrackHeight + 2 * borderPadding);

        int selectedRowStartY = -1;
        int selectedRowHeight = 0;
        for (int vis = 0; vis < countRows; vis++) 
        {
            int msgIdx = topMsg + vis;
            if (msgIdx == currentMessageIndex) 
            {
                selectedRowStartY = rowStartYs[vis];
                selectedRowHeight = actualHeights[vis];
                break;
            }
        }

        int scrollPos;

        if (selectedRowStartY != -1) 
        {
            int selectedRowCenterY = selectedRowStartY + selectedRowHeight / 2;
            scrollPos = selectedRowCenterY - barHeight / 2;
        }
        else 
        {
            int contentBeforeSelected = 0;
            for (int i = 0; i < currentMessageIndex; i++) 
            {
                contentBeforeSelected += estimatedHeights[i];
            }
            
            float scrollRatio = (float)contentBeforeSelected / (float)(totalContentHeight - availableHeight);
            scrollRatio = std::max(0.0f, std::min(1.0f, scrollRatio));
            
            int availableScrollSpace = scrollTrackHeight - barHeight;
            scrollPos = scrollTrackTop + (int)(availableScrollSpace * scrollRatio);
        }
        scrollPos = std::max(scrollTrackTop, std::min(scrollPos, scrollTrackBottom - barHeight));
        if (scrollPos >= scrollTrackTop && scrollPos + barHeight <= scrollTrackBottom)
            display->fillRect(scrollTrackX, scrollPos, barWidth, barHeight);
    }

    /*=== Preset priority List ===*/ 
    else if ((this->runState == PRESET_MESSAGE_RUN_STATE_ACTIVE) && (this->priorityCount > 0))
    {
        display->setColor(WHITE);
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->setFont(FONT_SMALL);

        /*==== Line spacing configuration ====*/ 
        const int EXTRA_ROW_SPACING = 8;
        const int baseRowSpacing = FONT_HEIGHT_SMALL + EXTRA_ROW_SPACING;

        int topMsg;
        std::vector<int> estimatedHeights;
        std::vector<int> actualHeights;
        std::vector<int> rowStartYs;
        int _visibleRows;

        drawHeader(display, x, y, buffer);

        const int listYOffset = y + FONT_HEIGHT_SMALL;
        int availableHeight = display->getHeight() - listYOffset;

        // Pre-calculate the height of all messages (supporting line breaks)
        for (int i = 0; i < priorityCount; i++) 
        {
            const char *msg = getPriorityByIndex(i);
            
            int scrollPadding = 15;
            int maxLineWidth = display->getWidth() - scrollPadding - x;
            std::vector<String> lines;
            String currentLine = "";
            
            String messageStr = String(msg);
            int startPos = 0;
            
            for (unsigned int j = 0; j <= messageStr.length(); j++) 
            {
                if (j == messageStr.length() || messageStr[j] == ' ') 
                {
                    String word = messageStr.substring(startPos, j);
                    String testLine = currentLine;
                    
                    if (testLine.length() > 0) 
                        testLine += " " + word;
                    else 
                        testLine = word;
                    
                    if (display->getStringWidth(testLine) > maxLineWidth && currentLine.length() > 0) 
                    {
                        lines.push_back(currentLine);
                        currentLine = word;
                    } 
                    else 
                        currentLine = testLine;
                
                    startPos = j + 1;
                }
            }
            
            if (currentLine.length() > 0) 
            {
                lines.push_back(currentLine);
            }
            
            int rowHeight = baseRowSpacing * std::max(1, (int)lines.size());
            estimatedHeights.push_back(rowHeight);
        }

        /*=== Rolling logic ===*/ 
        int totalHeight = 0;
        for (int i = 0; i < priorityCount; i++) 
        {
            totalHeight += estimatedHeights[i];
        }
        int avgHeight = totalHeight / priorityCount;
        int estimatedVisibleRows = availableHeight / avgHeight;
        
        estimatedVisibleRows = std::max(1, std::min(estimatedVisibleRows, priorityCount));
        
        if (priorityCount <= estimatedVisibleRows) 
        {
            topMsg = 0;
            _visibleRows = priorityCount;
        } 
        else 
        {
            int halfWindow = estimatedVisibleRows / 2;
            topMsg = currentpriorityIndex - halfWindow;
            
            if (topMsg < 0) 
                topMsg = 0;
            else if (topMsg + estimatedVisibleRows > priorityCount) 
                topMsg = priorityCount - estimatedVisibleRows;
            
            _visibleRows = estimatedVisibleRows;
        }

        int countRows = 0;
        int yCursor = listYOffset;
        actualHeights.clear();
        rowStartYs.clear();

        for (int i = topMsg; i < priorityCount && countRows < _visibleRows; i++) 
        {
            if (yCursor + estimatedHeights[i] <= listYOffset + availableHeight) 
            {
                countRows++;
                actualHeights.push_back(estimatedHeights[i]);
                rowStartYs.push_back(yCursor);
                yCursor += estimatedHeights[i];
            }
            else 
            {
                break;
            }
        }

        _visibleRows = countRows;

        /*=== Draw all rows ===*/ 
        yCursor = listYOffset;
        
        for (int vis = 0; vis < countRows; vis++) 
        {
            int msgIdx = topMsg + vis;
            int lineY = rowStartYs[vis];
            
            const char *msg = getPriorityByIndex(msgIdx);
            bool _highlight = (msgIdx == currentpriorityIndex);
            
            int scrollPadding = 15;
            int maxLineWidth = display->getWidth() - scrollPadding - x;
            
            std::vector<String> lines;
            String currentLine = "";
            
            String messageStr = String(msg);
            int startPos = 0;
            
            for (unsigned int j = 0; j <= messageStr.length(); j++) 
            {
                if (j == messageStr.length() || messageStr[j] == ' ') 
                {
                    String word = messageStr.substring(startPos, j);
                    String testLine = currentLine;
                    
                    if (testLine.length() > 0) 
                        testLine += " " + word;
                    else 
                        testLine = word;
                    
                    if (display->getStringWidth(testLine) > maxLineWidth && currentLine.length() > 0) 
                    {
                        lines.push_back(currentLine);
                        currentLine = word;
                    } 
                    else 
                        currentLine = testLine;
                
                    startPos = j + 1;
                }
            }
            
            if (currentLine.length() > 0) 
            {
                lines.push_back(currentLine);
            }
            
            int rowHeight = baseRowSpacing * std::max(1, (int)lines.size());
            
            if (_highlight) 
            {
                display->fillRect(x, lineY, display->getWidth() - scrollPadding, rowHeight);
                display->setColor(BLACK);
            }
            else 
            {
                display->setColor(WHITE);
            }
            
            int currentY = lineY;
            for (size_t i = 0; i < lines.size(); i++) 
            {
                String line = lines[i];
                int textYOffset = (baseRowSpacing - FONT_HEIGHT_SMALL) / 2;
                display->drawString(x, currentY + textYOffset, line);
                currentY += baseRowSpacing;
            }
            
            display->setColor(WHITE);
            yCursor += rowHeight;
        }

        /*=== Scroll bar calculation ===*/
        int borderPadding = 2; 
        int barWidth = 4;   

        int scrollTrackX = display->getWidth() - 6;
        int scrollTrackTop = listYOffset;
        int scrollTrackBottom = yCursor;
        int scrollTrackHeight = scrollTrackBottom - scrollTrackTop;

        int totalContentHeight = 0;
        for (int i = 0; i < priorityCount; i++) 
        {
            totalContentHeight += estimatedHeights[i];
        }

        int barHeight = baseRowSpacing;

        display->setColor(WHITE);
        display->drawRect(scrollTrackX - borderPadding, scrollTrackTop - borderPadding, barWidth + 2 * borderPadding, scrollTrackHeight + 2 * borderPadding);

        int selectedRowStartY = -1;
        int selectedRowHeight = 0;
        for (int vis = 0; vis < countRows; vis++) 
        {
            int msgIdx = topMsg + vis;
            if (msgIdx == currentpriorityIndex) 
            {
                selectedRowStartY = rowStartYs[vis];
                selectedRowHeight = actualHeights[vis];
                break;
            }
        }

        int scrollPos;

        if (selectedRowStartY != -1) 
        {
            int selectedRowCenterY = selectedRowStartY + selectedRowHeight / 2;
            scrollPos = selectedRowCenterY - barHeight / 2;
        }
        else 
        {
            int contentBeforeSelected = 0;
            for (int i = 0; i < currentpriorityIndex; i++) 
            {
                contentBeforeSelected += estimatedHeights[i];
            }
            
            float scrollRatio = (float)contentBeforeSelected / (float)(totalContentHeight - availableHeight);
            scrollRatio = std::max(0.0f, std::min(1.0f, scrollRatio));
            
            int availableScrollSpace = scrollTrackHeight - barHeight;
            scrollPos = scrollTrackTop + (int)(availableScrollSpace * scrollRatio);
        }
        scrollPos = std::max(scrollTrackTop, std::min(scrollPos, scrollTrackBottom - barHeight));
        if (scrollPos >= scrollTrackTop && scrollPos + barHeight <= scrollTrackBottom)
            display->fillRect(scrollTrackX, scrollPos, barWidth, barHeight);
    }

}

ProcessMessage PresetMessageModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (mp.decoded.portnum == meshtastic_PortNum_ROUTING_APP && waitingForAck) 
    {
        if (mp.decoded.request_id != 0) 
        {
            // Trigger screen refresh for ACK/NACK feedback
            UIFrameEvent e;
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            requestFocus();
            this->runState = PRESET_MESSAGE_RUN_STATE_ACK_NACK_RECEIVED;

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
            bool wasBroadcast = (this->lastSentNode == NODENUM_BROADCAST);

            // Identify the responding node
            if (wasBroadcast && mp.from != nodeDB->getNodeNum()) 
                this->incoming = mp.from; // Relayed by another node
            else 
                this->incoming = this->lastSentNode; // Direct reply

            // Final ACK confirmation logic
            this->ack = isAck && (wasBroadcast || isFromDest);
            waitingForAck = false;
            this->LastOperationTime = millis();
            this->notifyObservers(&e);
            setIntervalFromNow(3000);
        }
    }
    return ProcessMessage::CONTINUE;
}

int32_t PresetMessageModule::runOnce()
{
    // If we're in node selection, do nothing except keep alive
    if (this->runState == PRESET_MESSAGE_RUN_STATE_DESTINATION_SELECTION) 
    {
        return PRESET_INACTIVATE_AFTER_MS;
    }

    // Normal module disable/idle handling
    if ((this->runState == PRESET_MESSAGE_RUN_STATE_DISABLED) || (this->runState == PRESET_MESSAGE_RUN_STATE_INACTIVE)) 
    {
        return INT32_MAX;
    }

    UIFrameEvent e;
    if ((this->runState == PRESET_MESSAGE_RUN_STATE_ACK_NACK_RECEIVED)) 
    {
        currentMessageIndex = -1;
        this->runState = PRESET_MESSAGE_RUN_STATE_ACTIVE;
        requestFocus();
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
        screen->forceDisplay();
        return PRESET_INACTIVATE_AFTER_MS;
    }
    else if(this->runState == PRESET_MESSAGE_RUN_STATE_SENDING_ACTIVE)
    {
        if (!Throttle::isWithinTimespanMs(this->LastOperationTime, 15000)) 
        {
            this->runState = PRESET_MESSAGE_RUN_STATE_ACTIVE;
            requestFocus();
            currentMessageIndex = -1;
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            notifyObservers(&e);
            screen->forceDisplay();
            return PRESET_INACTIVATE_AFTER_MS;
        }
        return 1000;
    }
    else if ((this->runState == PRESET_MESSAGE_RUN_STATE_ACTIVE) && (!Throttle::isWithinTimespanMs(this->LastOperationTime, PRESET_INACTIVATE_AFTER_MS))) 
    {
        runState = PRESET_MESSAGE_RUN_STATE_INACTIVE;
        currentpriorityIndex = -1;
        currentMessageIndex = -1;
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
        screen->forceDisplay();
    } 
    else if (this->runState == PRESET_MESSAGE_RUN_STATE_ACTION_SELECT) 
    {
        if (strcmp(this->messages_array[this->currentMessageIndex], "[Exit]") == 0) 
        {
            runState = PRESET_MESSAGE_RUN_STATE_ACTIVE;
            currentMessageIndex = -1;
            requestFocus();
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            notifyObservers(&e);
            screen->forceDisplay();
            return PRESET_INACTIVATE_AFTER_MS;
        }
        else if(strcmp(this->messages_array[this->currentMessageIndex], "[This is my current location]") == 0)
        {
            InputEvent p;
            p.inputEvent = INPUT_BROKER_SEND_PING; 
            inputBroker->notifyObservers(&p);
            sendText(this->dest, this->channel, this->messages_array[this->currentMessageIndex], true);
            this->runState = PRESET_MESSAGE_RUN_STATE_SENDING_ACTIVE;
            this->LastOperationTime = millis(); 
            requestFocus();
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            this->notifyObservers(&e);
            screen->forceDisplay();
        }
        else if ((this->messageCount > this->currentMessageIndex) && (strlen(this->messages_array[this->currentMessageIndex]) > 0)) 
        {
            sendText(this->dest, this->channel, this->messages_array[this->currentMessageIndex], true);
            this->runState = PRESET_MESSAGE_RUN_STATE_SENDING_ACTIVE;
            this->LastOperationTime = millis(); 
            requestFocus();
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            this->notifyObservers(&e);
            screen->forceDisplay();
        } 
        else 
        {
            runState = PRESET_MESSAGE_RUN_STATE_INACTIVE;
            currentpriorityIndex = -1;
            currentMessageIndex = -1;
            e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
            notifyObservers(&e);
            screen->forceDisplay();
        }
        return 1000;
    }
    else if (this->currentpriorityIndex == -1) 
    {
        this->runState = PRESET_MESSAGE_RUN_STATE_ACTIVE;
        int selectDestination = 0;
        for (int i = 0; i < this->priorityCount; ++i) 
        {
            if (strcmp(this->messages_priority[i], "[Select Destination]") == 0) 
            {
                selectDestination = i;
                break;
            }
        }
        this->currentpriorityIndex = selectDestination;
        requestFocus();
        e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
        notifyObservers(&e);
        screen->forceDisplay();
        return PRESET_INACTIVATE_AFTER_MS;
    }
    else if(this->runState == PRESET_MESSAGE_RUN_STATE_ACTIVE)
        return PRESET_INACTIVATE_AFTER_MS;
    return INT32_MAX;
}

#endif
