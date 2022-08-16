#include "configuration.h"
#if HAS_SCREEN
#include "CannedMessageModule.h"
#include "FSCommon.h"
#include "MeshService.h"
#include "PowerFSM.h" // neede for button bypass
#include "mesh/generated/cannedmessages.pb.h"

// TODO: reuse defined from Screen.cpp
#define FONT_SMALL ArialMT_Plain_10
#define FONT_MEDIUM ArialMT_Plain_16
#define FONT_LARGE ArialMT_Plain_24

// Remove Canned message screen if no action is taken for some milliseconds
#define INACTIVATE_AFTER_MS 20000

static const char *cannedMessagesConfigFile = "/prefs/cannedConf.proto";

CannedMessageModuleConfig cannedMessageModuleConfig;

CannedMessageModule *cannedMessageModule;

// TODO: move it into NodeDB.h!
extern bool loadProto(const char *filename, size_t protoSize, size_t objSize, const pb_msgdesc_t *fields, void *dest_struct);
extern bool saveProto(const char *filename, size_t protoSize, size_t objSize, const pb_msgdesc_t *fields,
                      const void *dest_struct);

CannedMessageModule::CannedMessageModule()
    : SinglePortModule("canned", PortNum_TEXT_MESSAGE_APP), concurrency::OSThread("CannedMessageModule")
{
    if (moduleConfig.canned_message.enabled) {
        this->loadProtoForModule();
        if (this->splitConfiguredMessages() <= 0) {
            DEBUG_MSG("CannedMessageModule: No messages are configured. Module is disabled\n");
            this->runState = CANNED_MESSAGE_RUN_STATE_DISABLED;
        } else {
            DEBUG_MSG("CannedMessageModule is enabled\n");
            this->inputObserver.observe(inputBroker);
        }
    }
}

/**
 * @brief Items in array this->messages will be set to be pointing on the right
 *     starting points of the string this->messageStore
 *
 * @return int Returns the number of messages found.
 */
// FIXME: This is just one set of messages now
int CannedMessageModule::splitConfiguredMessages()
{
    int messageIndex = 0;
    int i = 0;

    // collect all the message parts
    strcpy(this->messageStore, cannedMessageModuleConfig.messages);

    // The first message points to the beginning of the store.
    this->messages[messageIndex++] = this->messageStore;
    int upTo = strlen(this->messageStore) - 1;

    while (i < upTo) {
        if (this->messageStore[i] == '|') {
            // Message ending found, replace it with string-end character.
            this->messageStore[i] = '\0';
            DEBUG_MSG("CannedMessage %d is: '%s'\n", messageIndex - 1, this->messages[messageIndex - 1]);

            // hit our max messages, bail
            if (messageIndex >= CANNED_MESSAGE_MODULE_MESSAGE_MAX_COUNT) {
                this->messagesCount = messageIndex;
                return this->messagesCount;
            }

            // Next message starts after pipe (|) just found.
            this->messages[messageIndex++] = (this->messageStore + i + 1);
        }
        i += 1;
    }
    if (strlen(this->messages[messageIndex - 1]) > 0) {
        // We have a last message.
        DEBUG_MSG("CannedMessage %d is: '%s'\n", messageIndex - 1, this->messages[messageIndex - 1]);
        this->messagesCount = messageIndex;
    } else {
        this->messagesCount = messageIndex - 1;
    }

    return this->messagesCount;
}

int CannedMessageModule::handleInputEvent(const InputEvent *event)
{
    if ((strlen(moduleConfig.canned_message.allow_input_source) > 0) &&
        (strcmp(moduleConfig.canned_message.allow_input_source, event->source) != 0) &&
        (strcmp(moduleConfig.canned_message.allow_input_source, "_any") != 0)) {
        // Event source is not accepted.
        // Event only accepted if source matches the configured one, or
        //   the configured one is "_any" (or if there is no configured
        //   source at all)
        return 0;
    }

    bool validEvent = false;
    if (event->inputEvent == static_cast<char>(ModuleConfig_CannedMessageConfig_InputEventChar_KEY_UP)) {
        DEBUG_MSG("Canned message event UP\n");
        this->runState = CANNED_MESSAGE_RUN_STATE_ACTION_UP;
        validEvent = true;
    }
    if (event->inputEvent == static_cast<char>(ModuleConfig_CannedMessageConfig_InputEventChar_KEY_DOWN)) {
        DEBUG_MSG("Canned message event DOWN\n");
        this->runState = CANNED_MESSAGE_RUN_STATE_ACTION_DOWN;
        validEvent = true;
    }
    if (event->inputEvent == static_cast<char>(ModuleConfig_CannedMessageConfig_InputEventChar_KEY_SELECT)) {
        DEBUG_MSG("Canned message event Select\n");
        // when inactive, call the onebutton shortpress instead. Activate Module only on up/down
        if ((this->runState == CANNED_MESSAGE_RUN_STATE_INACTIVE) || (this->runState == CANNED_MESSAGE_RUN_STATE_DISABLED)) {
            powerFSM.trigger(EVENT_PRESS);
        } else {
            this->runState = CANNED_MESSAGE_RUN_STATE_ACTION_SELECT;
            validEvent = true;
        }
    }

    if (validEvent) {
        // Let runOnce to be called immediately.
        setIntervalFromNow(0);
    }

    return 0;
}

void CannedMessageModule::sendText(NodeNum dest, const char *message, bool wantReplies)
{
    MeshPacket *p = allocDataPacket();
    p->to = dest;
    p->want_ack = true;
    p->decoded.payload.size = strlen(message);
    memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);
    if (moduleConfig.canned_message.send_bell) {
        p->decoded.payload.bytes[p->decoded.payload.size - 1] = 7; // Bell character
        p->decoded.payload.bytes[p->decoded.payload.size] = '\0';  // Bell character
        p->decoded.payload.size++;
    }

    DEBUG_MSG("Sending message id=%d, msg=%.*s\n", p->id, p->decoded.payload.size, p->decoded.payload.bytes);

    service.sendToMesh(p);
}

int32_t CannedMessageModule::runOnce()
{
    if ((!moduleConfig.canned_message.enabled) || (this->runState == CANNED_MESSAGE_RUN_STATE_DISABLED) ||
        (this->runState == CANNED_MESSAGE_RUN_STATE_INACTIVE)) {
        return 30000; // TODO: should return MAX_VAL
    }
    DEBUG_MSG("Check status\n");
    UIFrameEvent e = {false, true};
    if (this->runState == CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE) {
        // TODO: might have some feedback of sendig state
        this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
        e.frameChanged = true;
        this->currentMessageIndex = -1;
        this->notifyObservers(&e);
    } else if ((this->runState == CANNED_MESSAGE_RUN_STATE_ACTIVE) && (millis() - this->lastTouchMillis) > INACTIVATE_AFTER_MS) {
        // Reset module
        DEBUG_MSG("Reset due the lack of activity.\n");
        e.frameChanged = true;
        this->currentMessageIndex = -1;
        this->runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
        this->notifyObservers(&e);
    } else if (this->currentMessageIndex == -1) {
        this->currentMessageIndex = 0;
        DEBUG_MSG("First touch (%d):%s\n", this->currentMessageIndex, this->getCurrentMessage());
        e.frameChanged = true;
        this->runState = CANNED_MESSAGE_RUN_STATE_ACTIVE;
    } else if (this->runState == CANNED_MESSAGE_RUN_STATE_ACTION_SELECT) {
        sendText(NODENUM_BROADCAST, this->messages[this->currentMessageIndex], true);
        this->runState = CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE;
        this->currentMessageIndex = -1;
        this->notifyObservers(&e);
        return 2000;
    } else if (this->runState == CANNED_MESSAGE_RUN_STATE_ACTION_UP) {
        this->currentMessageIndex = getPrevIndex();
        this->runState = CANNED_MESSAGE_RUN_STATE_ACTIVE;
        DEBUG_MSG("MOVE UP (%d):%s\n", this->currentMessageIndex, this->getCurrentMessage());
    } else if (this->runState == CANNED_MESSAGE_RUN_STATE_ACTION_DOWN) {
        this->currentMessageIndex = this->getNextIndex();
        this->runState = CANNED_MESSAGE_RUN_STATE_ACTIVE;
        DEBUG_MSG("MOVE DOWN (%d):%s\n", this->currentMessageIndex, this->getCurrentMessage());
    }

    if (this->runState == CANNED_MESSAGE_RUN_STATE_ACTIVE) {
        this->lastTouchMillis = millis();
        this->notifyObservers(&e);
        return INACTIVATE_AFTER_MS;
    }

    return 30000; // TODO: should return MAX_VAL
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
bool CannedMessageModule::shouldDraw()
{
    if (!moduleConfig.canned_message.enabled) {
        return false;
    }
    return (currentMessageIndex != -1) || (this->runState != CANNED_MESSAGE_RUN_STATE_INACTIVE);
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

void CannedMessageModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    displayedNodeNum = 0; // Not currently showing a node pane

    if (cannedMessageModule->runState == CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE) {
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->setFont(FONT_MEDIUM);
        display->drawString(display->getWidth() / 2 + x, 0 + y + 12, "Sending...");
    } else if (cannedMessageModule->runState == CANNED_MESSAGE_RUN_STATE_DISABLED) {
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->setFont(FONT_SMALL);
        display->drawString(10 + x, 0 + y + 16, "Canned Message\nModule disabled.");
    } else {
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->setFont(FONT_SMALL);
        display->drawString(0 + x, 0 + y, cannedMessageModule->getPrevMessage());
        display->setFont(FONT_MEDIUM);
        display->drawString(0 + x, 0 + y + 8, cannedMessageModule->getCurrentMessage());
        display->setFont(FONT_SMALL);
        display->drawString(0 + x, 0 + y + 24, cannedMessageModule->getNextMessage());
    }
}

void CannedMessageModule::loadProtoForModule()
{
    if (!loadProto(cannedMessagesConfigFile, CannedMessageModuleConfig_size, sizeof(cannedMessagesConfigFile),
                   CannedMessageModuleConfig_fields, &cannedMessageModuleConfig)) {
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

#ifdef FS
    FS.mkdir("/prefs");
#endif

    okay &= saveProto(cannedMessagesConfigFile, CannedMessageModuleConfig_size, sizeof(CannedMessageModuleConfig),
                      CannedMessageModuleConfig_fields, &cannedMessageModuleConfig);

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
AdminMessageHandleResult CannedMessageModule::handleAdminMessageForModule(const MeshPacket &mp, AdminMessage *request,
                                                                          AdminMessage *response)
{
    AdminMessageHandleResult result;

    switch (request->which_variant) {
    case AdminMessage_get_canned_message_module_messages_request_tag:
        DEBUG_MSG("Client is getting radio canned messages\n");
        this->handleGetCannedMessageModuleMessages(mp, response);
        result = AdminMessageHandleResult::HANDLED_WITH_RESPONSE;
        break;

    case AdminMessage_set_canned_message_module_messages_tag:
        DEBUG_MSG("Client is setting radio canned messages\n");
        this->handleSetCannedMessageModuleMessages(request->set_canned_message_module_messages);
        result = AdminMessageHandleResult::HANDLED;
        break;

    default:
        result = AdminMessageHandleResult::NOT_HANDLED;
    }

    return result;
}

void CannedMessageModule::handleGetCannedMessageModuleMessages(const MeshPacket &req, AdminMessage *response)
{
    DEBUG_MSG("*** handleGetCannedMessageModuleMessages\n");
    assert(req.decoded.want_response);

    response->which_variant = AdminMessage_get_canned_message_module_messages_response_tag;
    strcpy(response->get_canned_message_module_messages_response, cannedMessageModuleConfig.messages);
}


void CannedMessageModule::handleSetCannedMessageModuleMessages(const char *from_msg)
{
    int changed = 0;

    if (*from_msg) {
        changed |= strcmp(cannedMessageModuleConfig.messages, from_msg);
        strcpy(cannedMessageModuleConfig.messages, from_msg);
        DEBUG_MSG("*** from_msg.text:%s\n", from_msg);
    }

    if (changed) {
        this->saveProtoForModule();
    }
}

#endif