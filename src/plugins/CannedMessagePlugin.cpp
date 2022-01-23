#include "configuration.h"
#include "CannedMessagePlugin.h"
#include "MeshService.h"

// TODO: reuse defined from Screen.cpp
#define FONT_SMALL ArialMT_Plain_10
#define FONT_MEDIUM ArialMT_Plain_16
#define FONT_LARGE ArialMT_Plain_24

// Remove Canned message screen if no action is taken for some milliseconds
#define INACTIVATE_AFTER_MS 20000

CannedMessagePlugin *cannedMessagePlugin;

CannedMessagePlugin::CannedMessagePlugin()
    : SinglePortPlugin("canned", PortNum_TEXT_MESSAGE_APP),
    concurrency::OSThread("CannedMessagePlugin")
{
    if (radioConfig.preferences.canned_message_plugin_enabled)
    {
        if(this->splitConfiguredMessages() <= 0)
        {
            radioConfig.preferences.canned_message_plugin_enabled = false;
            DEBUG_MSG("CannedMessagePlugin: No messages are configured. Plugin is disabled\n");
            return;
        }
        this->inputObserver.observe(inputBroker);
    }
}

/**
 * @brief Items in array this->messages will be set to be pointing on the right
 *     starting points of the string this->messageStore
 * 
 * @return int Returns the number of messages found.
 */
int CannedMessagePlugin::splitConfiguredMessages()
{
    int messageIndex = 0;
    int i = 0;

    strncpy(
        this->messageStore,
        radioConfig.preferences.canned_message_plugin_messages,
        CANNED_MESSAGE_PLUGIN_MESSAGES_SIZE);

    this->messages[messageIndex++] =
        this->messageStore;
    int upTo =
        strlen(this->messageStore) - 1;

    while (i < upTo)
    {
        if (this->messageStore[i] == '|')
        {
            // Message ending found, replace it with string-end character.
            this->messageStore[i] = '\0';
            DEBUG_MSG("CannedMessage %d is: '%s'\n",
                messageIndex-1, this->messages[messageIndex-1]);

            if (messageIndex >= CANNED_MESSAGE_PLUGIN_MESSAGE_MAX_COUNT)
            {
                this->messagesCount = messageIndex;
                return this->messagesCount;
            }

            // Next message starts after pipe (|) just found.
            this->messages[messageIndex++] =
                (this->messageStore + i + 1);
        }
        i += 1;
    }
    if (strlen(this->messages[messageIndex-1]) > 0)
    {
        DEBUG_MSG("CannedMessage %d is: '%s'\n",
            messageIndex-1, this->messages[messageIndex-1]);
        this->messagesCount = messageIndex;
    }
    else
    {
        this->messagesCount = messageIndex-1;
    }

    return this->messagesCount;
}

int CannedMessagePlugin::handleInputEvent(const InputEvent *event)
{
    if (
        (strlen(radioConfig.preferences.canned_message_plugin_allow_input_source) > 0) &&
        (strcmp(radioConfig.preferences.canned_message_plugin_allow_input_source, event->source) != 0) &&
        (strcmp(radioConfig.preferences.canned_message_plugin_allow_input_source, "_any") != 0))
    {
        // Event source is not accepted.
        return 0;
    }

    bool validEvent = false;
    if (event->inputEvent == static_cast<char>(InputEventChar_KEY_UP))
    {
        DEBUG_MSG("Canned message event UP\n");
        this->runState = CANNED_MESSAGE_RUN_STATE_ACTION_UP;
        validEvent = true;
    }
    if (event->inputEvent == static_cast<char>(InputEventChar_KEY_DOWN))
    {
        DEBUG_MSG("Canned message event DOWN\n");
        this->runState = CANNED_MESSAGE_RUN_STATE_ACTION_DOWN;
        validEvent = true;
    }
    if (event->inputEvent == static_cast<char>(InputEventChar_KEY_SELECT))
    {
        DEBUG_MSG("Canned message event Select\n");
        this->runState = CANNED_MESSAGE_RUN_STATE_ACTION_SELECT;
        validEvent = true;
    }

    if (validEvent)
    {
        // Let runOnce to be called immediately.
        setIntervalFromNow(0);
    }

    return 0;
}

void CannedMessagePlugin::sendText(NodeNum dest,
      const char* message,
      bool wantReplies)
{
    MeshPacket *p = allocDataPacket();
    p->to = dest;
    p->want_ack = true;
    p->decoded.payload.size = strlen(message);
    memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);
    if (radioConfig.preferences.canned_message_plugin_send_bell)
    {
        p->decoded.payload.bytes[p->decoded.payload.size-1] = 7; // Bell character
        p->decoded.payload.bytes[p->decoded.payload.size] = '\0'; // Bell character
        p->decoded.payload.size++;
    }


//    PacketId prevPacketId = p->id; // In case we need it later.

    DEBUG_MSG("Sending message id=%d, msg=%.*s\n",
      p->id, p->decoded.payload.size, p->decoded.payload.bytes);

    service.sendToMesh(p);
}

int32_t CannedMessagePlugin::runOnce()
{
    if ((!radioConfig.preferences.canned_message_plugin_enabled)
        || (this->runState == CANNED_MESSAGE_RUN_STATE_INACTIVE))
    {
        return 30000; // TODO: should return MAX_VAL
    }
    DEBUG_MSG("Check status\n");
    UIFrameEvent e = {false, true};
    if (this->runState == CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE)
    {
        // TODO: might have some feedback of sendig state
        this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
        e.frameChanged = true;
        this->currentMessageIndex = -1;
        this->notifyObservers(&e);
    }
    else if (
        (this->runState == CANNED_MESSAGE_RUN_STATE_ACTIVE)
         && (millis() - this->lastTouchMillis) > INACTIVATE_AFTER_MS)
    {
        // Reset plugin
        DEBUG_MSG("Reset due the lack of activity.\n");
        e.frameChanged = true;
        this->currentMessageIndex = -1;
        this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
        this->notifyObservers(&e);
    }
    else if (this->currentMessageIndex == -1)
    {
        this->currentMessageIndex = 0;
        DEBUG_MSG("First touch.\n");
        e.frameChanged = true;
        this->runState = CANNED_MESSAGE_RUN_STATE_ACTIVE;
    }
    else if (this->runState == CANNED_MESSAGE_RUN_STATE_ACTION_SELECT)
    {
        sendText(
            NODENUM_BROADCAST,
            this->messages[this->currentMessageIndex],
            true);
        this->runState = CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE;
        this->currentMessageIndex = -1;
        this->notifyObservers(&e);
        return 2000;
    }
    else if (this->runState == CANNED_MESSAGE_RUN_STATE_ACTION_UP)
    {
        this->currentMessageIndex = getPrevIndex();
        this->runState = CANNED_MESSAGE_RUN_STATE_ACTIVE;
        DEBUG_MSG("MOVE UP\n");
    }
    else if (this->runState == CANNED_MESSAGE_RUN_STATE_ACTION_DOWN)
    {
        this->currentMessageIndex = this->getNextIndex();
        this->runState = CANNED_MESSAGE_RUN_STATE_ACTIVE;
        DEBUG_MSG("MOVE DOWN\n");
    }

    if (this->runState == CANNED_MESSAGE_RUN_STATE_ACTIVE)
    {
        this->lastTouchMillis = millis();
        this->notifyObservers(&e);
        return INACTIVATE_AFTER_MS;
    }

    return 30000; // TODO: should return MAX_VAL
}

const char* CannedMessagePlugin::getCurrentMessage()
{
    return this->messages[this->currentMessageIndex];
}
const char* CannedMessagePlugin::getPrevMessage()
{
    return this->messages[this->getPrevIndex()];
}
const char* CannedMessagePlugin::getNextMessage()
{
    return this->messages[this->getNextIndex()];
}
bool CannedMessagePlugin::shouldDraw()
{
    if (!radioConfig.preferences.canned_message_plugin_enabled)
    {
        return false;
    }
    return (currentMessageIndex != -1) || (this->runState != CANNED_MESSAGE_RUN_STATE_INACTIVE);
}

int CannedMessagePlugin::getNextIndex()
{
    if (this->currentMessageIndex >= (this->messagesCount -1))
    {
        return 0;
    }
    else
    {
        return this->currentMessageIndex + 1;
    }
}

int CannedMessagePlugin::getPrevIndex()
{
    if (this->currentMessageIndex <= 0)
    {
        return this->messagesCount - 1;
    }
    else
    {
        return this->currentMessageIndex - 1;
    }
}

void CannedMessagePlugin::drawFrame(
    OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    displayedNodeNum = 0; // Not currently showing a node pane

    if (cannedMessagePlugin->runState == CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE)
    {
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->setFont(FONT_MEDIUM);
        display->drawString(display->getWidth()/2 + x, 0 + y + 12, "Sending...");
    }
    else
    {
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->setFont(FONT_SMALL);
        display->drawString(0 + x, 0 + y, cannedMessagePlugin->getPrevMessage());
        display->setFont(FONT_MEDIUM);
        display->drawString(0 + x, 0 + y + 8, cannedMessagePlugin->getCurrentMessage());
        display->setFont(FONT_SMALL);
        display->drawString(0 + x, 0 + y + 24, cannedMessagePlugin->getNextMessage());
    }
}

