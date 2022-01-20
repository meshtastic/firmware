#include "configuration.h"
#include "CannedMessagePlugin.h"
#include "MeshService.h"

// TODO: reuse defined from Screen.cpp
#define FONT_SMALL ArialMT_Plain_10
#define FONT_MEDIUM ArialMT_Plain_16
#define FONT_LARGE ArialMT_Plain_24

// Remove Canned message screen if no action is taken for some milliseconds
#define INACTIVATE_AFTER_MS 20000

static const char *cannedMessagesPart1file = "/prefs/cannedmessagespart1.proto";
static const char *cannedMessagesPart2file = "/prefs/cannedmessagespart2.proto";
static const char *cannedMessagesPart3file = "/prefs/cannedmessagespart3.proto";
static const char *cannedMessagesPart4file = "/prefs/cannedmessagespart4.proto";
static const char *cannedMessagesPart5file = "/prefs/cannedmessagespart5.proto";

CannedMessagePluginMessagePart1 cannedMessagePluginMessagePart1;
CannedMessagePluginMessagePart2 cannedMessagePluginMessagePart2;
CannedMessagePluginMessagePart3 cannedMessagePluginMessagePart3;
CannedMessagePluginMessagePart4 cannedMessagePluginMessagePart4;
CannedMessagePluginMessagePart5 cannedMessagePluginMessagePart5;

CannedMessagePlugin *cannedMessagePlugin;

// TODO: move it into NodeDB.h!
extern bool loadProto(const char *filename, size_t protoSize, size_t objSize, const pb_msgdesc_t *fields, void *dest_struct);
extern bool saveProto(const char *filename, size_t protoSize, size_t objSize, const pb_msgdesc_t *fields, const void *dest_struct);

CannedMessagePlugin::CannedMessagePlugin()
    : ProtobufPlugin("canned", PortNum_TEXT_MESSAGE_APP, AdminMessage_fields),
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
 *     starting points of the string radioConfig.preferences.canned_message_plugin_messages
 * @return int Returns the number of messages found.
 */
int CannedMessagePlugin::splitConfiguredMessages()
{
    int messageIndex = 0;
    int i = 0;
    char all[1001];

    // get all of the message parts
    strcpy(all, cannedMessagePluginMessagePart1.text);
    strcat(all, cannedMessagePluginMessagePart2.text);
    strcat(all, cannedMessagePluginMessagePart3.text);
    strcat(all, cannedMessagePluginMessagePart4.text);
    strcat(all, cannedMessagePluginMessagePart5.text);

    this->messages[messageIndex++] = all;
    int upTo = strlen(all) - 1;

    while (i < upTo)
    {
	// look for delimiter
        if (all[i] == '|')
        {
            // Message ending found, replace it with string-end character.
            all[i] = '\0';
            DEBUG_MSG("CannedMessage %d is: '%s'\n",
                messageIndex-1, this->messages[messageIndex-1]);

	    // hit our max messages, bail
            if (messageIndex >= CANNED_MESSAGE_PLUGIN_MESSAGE_MAX_COUNT)
            {
                this->messagesCount = messageIndex;
                return this->messagesCount;
            }

            // Next message starts after pipe (|) just found.
            this->messages[messageIndex++] = (all + i + 1);
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

void CannedMessagePlugin::loadProtoForPlugin()
{
    if (!loadProto(cannedMessagesPart1file, CannedMessagePluginMessagePart1_size, sizeof(cannedMessagesPart1file), CannedMessagePluginMessagePart1_fields, &cannedMessagePluginMessagePart1)) {
        memset(cannedMessagePluginMessagePart1.text, 0, sizeof(cannedMessagePluginMessagePart1.text));
    }
    if (!loadProto(cannedMessagesPart2file, CannedMessagePluginMessagePart2_size, sizeof(cannedMessagesPart2file), CannedMessagePluginMessagePart2_fields, &cannedMessagePluginMessagePart2)) {
        memset(cannedMessagePluginMessagePart2.text, 0, sizeof(cannedMessagePluginMessagePart2.text));
    }
    if (!loadProto(cannedMessagesPart3file, CannedMessagePluginMessagePart3_size, sizeof(cannedMessagesPart3file), CannedMessagePluginMessagePart3_fields, &cannedMessagePluginMessagePart3)) {
        memset(cannedMessagePluginMessagePart3.text, 0, sizeof(cannedMessagePluginMessagePart3.text));
    }
    if (!loadProto(cannedMessagesPart4file, CannedMessagePluginMessagePart4_size, sizeof(cannedMessagesPart4file), CannedMessagePluginMessagePart4_fields, &cannedMessagePluginMessagePart4)) {
        memset(cannedMessagePluginMessagePart4.text, 0, sizeof(cannedMessagePluginMessagePart4.text));
    }
    if (!loadProto(cannedMessagesPart5file, CannedMessagePluginMessagePart5_size, sizeof(cannedMessagesPart5file), CannedMessagePluginMessagePart5_fields, &cannedMessagePluginMessagePart5)) {
        memset(cannedMessagePluginMessagePart5.text, 0, sizeof(cannedMessagePluginMessagePart5.text));
    }
}

bool CannedMessagePlugin::saveProtoForPlugin()
{
    bool okay = true;

    okay &= saveProto(cannedMessagesPart1file, CannedMessagePluginMessagePart1_size, sizeof(CannedMessagePluginMessagePart1), CannedMessagePluginMessagePart1_fields, &cannedMessagePluginMessagePart1);
    okay &= saveProto(cannedMessagesPart2file, CannedMessagePluginMessagePart2_size, sizeof(CannedMessagePluginMessagePart2), CannedMessagePluginMessagePart2_fields, &cannedMessagePluginMessagePart2);
    okay &= saveProto(cannedMessagesPart3file, CannedMessagePluginMessagePart3_size, sizeof(CannedMessagePluginMessagePart3), CannedMessagePluginMessagePart3_fields, &cannedMessagePluginMessagePart3);
    okay &= saveProto(cannedMessagesPart4file, CannedMessagePluginMessagePart4_size, sizeof(CannedMessagePluginMessagePart4), CannedMessagePluginMessagePart4_fields, &cannedMessagePluginMessagePart4);
    okay &= saveProto(cannedMessagesPart5file, CannedMessagePluginMessagePart5_size, sizeof(CannedMessagePluginMessagePart5), CannedMessagePluginMessagePart5_fields, &cannedMessagePluginMessagePart5);

    return okay;
}

void CannedMessagePlugin::installProtoDefaultsForPlugin()
{
    // TODO: resolve code duplication!
    memset(cannedMessagePluginMessagePart1.text, 0, sizeof(cannedMessagePluginMessagePart1.text));
    memset(cannedMessagePluginMessagePart2.text, 0, sizeof(cannedMessagePluginMessagePart2.text));
    memset(cannedMessagePluginMessagePart3.text, 0, sizeof(cannedMessagePluginMessagePart3.text));
    memset(cannedMessagePluginMessagePart4.text, 0, sizeof(cannedMessagePluginMessagePart4.text));
    memset(cannedMessagePluginMessagePart5.text, 0, sizeof(cannedMessagePluginMessagePart5.text));
}

bool CannedMessagePlugin::handleAdminMessageForPlugin(const MeshPacket &mp, AdminMessage *r)
{
    bool handled = false;
    switch (r->which_variant)
    {
    case AdminMessage_get_canned_message_plugin_part1_request_tag:
        DEBUG_MSG("Client is getting radio canned message part1\n");
        handleGetCannedMessagePluginPart1(mp);
        handled = true;
        break;

    case AdminMessage_get_canned_message_plugin_part2_request_tag:
        DEBUG_MSG("Client is getting radio canned message part2\n");
        handleGetCannedMessagePluginPart2(mp);
        handled = true;
        break;

    case AdminMessage_get_canned_message_plugin_part3_request_tag:
        DEBUG_MSG("Client is getting radio canned message part3\n");
        handleGetCannedMessagePluginPart3(mp);
        handled = true;
        break;

    case AdminMessage_get_canned_message_plugin_part4_request_tag:
        DEBUG_MSG("Client is getting radio canned message part4\n");
        handleGetCannedMessagePluginPart4(mp);
        handled = true;
        break;

    case AdminMessage_get_canned_message_plugin_part5_request_tag:
        DEBUG_MSG("Client is getting radio canned message part5\n");
        handleGetCannedMessagePluginPart5(mp);
        handled = true;
        break;

    case AdminMessage_set_canned_message_plugin_part1_tag:
        DEBUG_MSG("Client is setting radio canned message part 1\n");
        handleSetCannedMessagePluginPart1(r->set_canned_message_plugin_part1);
        handled = true;
        break;

    case AdminMessage_set_canned_message_plugin_part2_tag:
        DEBUG_MSG("Client is setting radio canned message part 2\n");
        handleSetCannedMessagePluginPart2(r->set_canned_message_plugin_part2);
        handled = true;
        break;

    case AdminMessage_set_canned_message_plugin_part3_tag:
        DEBUG_MSG("Client is setting radio canned message part 3\n");
        handleSetCannedMessagePluginPart3(r->set_canned_message_plugin_part3);
        handled = true;
        break;

    case AdminMessage_set_canned_message_plugin_part4_tag:
        DEBUG_MSG("Client is setting radio canned message part 4\n");
        handleSetCannedMessagePluginPart4(r->set_canned_message_plugin_part4);
        handled = true;
        break;

    case AdminMessage_set_canned_message_plugin_part5_tag:
        DEBUG_MSG("Client is setting radio canned message part 5\n");
        handleSetCannedMessagePluginPart5(r->set_canned_message_plugin_part5);
        handled = true;
        break;
    
    default:
        break;
    }

    return handled;
}

void CannedMessagePlugin::handleGetCannedMessagePluginPart1(const MeshPacket &req)
{
    DEBUG_MSG("*** handleGetCannedMessagePluginPart1\n");
    if (req.decoded.want_response) {
        // We create the reply here
        AdminMessage r = AdminMessage_init_default;
        r.get_canned_message_plugin_part1_response = cannedMessagePluginMessagePart1;
	DEBUG_MSG("*** cannedMessagePluginMessagePart1.text:%s\n", cannedMessagePluginMessagePart1.text);
        r.which_variant = AdminMessage_get_canned_message_plugin_part1_response_tag;
        myReply = allocDataProtobuf(r);
    }
}

void CannedMessagePlugin::handleGetCannedMessagePluginPart2(const MeshPacket &req)
{
    if (req.decoded.want_response) {
        // We create the reply here
        AdminMessage r = AdminMessage_init_default;
        r.get_canned_message_plugin_part2_response = cannedMessagePluginMessagePart2;
        r.which_variant = AdminMessage_get_canned_message_plugin_part2_response_tag;
        myReply = allocDataProtobuf(r);
    }
}

void CannedMessagePlugin::handleGetCannedMessagePluginPart3(const MeshPacket &req)
{
    if (req.decoded.want_response) {
        // We create the reply here
        AdminMessage r = AdminMessage_init_default;
        r.get_canned_message_plugin_part3_response = cannedMessagePluginMessagePart3;
        r.which_variant = AdminMessage_get_canned_message_plugin_part3_response_tag;
        myReply = allocDataProtobuf(r);
    }
}

void CannedMessagePlugin::handleGetCannedMessagePluginPart4(const MeshPacket &req)
{
    if (req.decoded.want_response) {
        // We create the reply here
        AdminMessage r = AdminMessage_init_default;
        r.get_canned_message_plugin_part4_response = cannedMessagePluginMessagePart4;
        r.which_variant = AdminMessage_get_canned_message_plugin_part4_response_tag;
        myReply = allocDataProtobuf(r);
    }
}

void CannedMessagePlugin::handleGetCannedMessagePluginPart5(const MeshPacket &req)
{
    if (req.decoded.want_response) {
        // We create the reply here
        AdminMessage r = AdminMessage_init_default;
        r.get_canned_message_plugin_part5_response = cannedMessagePluginMessagePart5;
        r.which_variant = AdminMessage_get_canned_message_plugin_part5_response_tag;
        myReply = allocDataProtobuf(r);
    }
}

void CannedMessagePlugin::handleSetCannedMessagePluginPart1(const CannedMessagePluginMessagePart1 &from_msg)
{
    int changed = 0;

    if (*from_msg.text) {
        changed |= strcmp(cannedMessagePluginMessagePart1.text, from_msg.text);
        strcpy(cannedMessagePluginMessagePart1.text, from_msg.text);
	DEBUG_MSG("*** from_msg.text:%s\n", from_msg.text);
    }

    if (changed) // If nothing really changed, don't broadcast on the network or write to flash
        service.reloadConfig(); // TODO: does this make sense?
}

void CannedMessagePlugin::handleSetCannedMessagePluginPart2(const CannedMessagePluginMessagePart2 &from_msg)
{
    int changed = 0;

    if (*from_msg.text) {
        changed |= strcmp(cannedMessagePluginMessagePart2.text, from_msg.text);
        strcpy(cannedMessagePluginMessagePart2.text, from_msg.text);
    }

    if (changed) // If nothing really changed, don't broadcast on the network or write to flash
        service.reloadConfig(); // TODO: does this make sense?
}

void CannedMessagePlugin::handleSetCannedMessagePluginPart3(const CannedMessagePluginMessagePart3 &from_msg)
{
    int changed = 0;

    if (*from_msg.text) {
        changed |= strcmp(cannedMessagePluginMessagePart3.text, from_msg.text);
        strcpy(cannedMessagePluginMessagePart3.text, from_msg.text);
    }

    if (changed) // If nothing really changed, don't broadcast on the network or write to flash
        service.reloadConfig(); // TODO: does this make sense?
}

void CannedMessagePlugin::handleSetCannedMessagePluginPart4(const CannedMessagePluginMessagePart4 &from_msg)
{
    int changed = 0;

    if (*from_msg.text) {
        changed |= strcmp(cannedMessagePluginMessagePart4.text, from_msg.text);
        strcpy(cannedMessagePluginMessagePart4.text, from_msg.text);
    }

    if (changed) // If nothing really changed, don't broadcast on the network or write to flash
        service.reloadConfig(); // TODO: does this make sense?
}

void CannedMessagePlugin::handleSetCannedMessagePluginPart5(const CannedMessagePluginMessagePart5 &from_msg)
{
    int changed = 0;

    if (*from_msg.text) {
        changed |= strcmp(cannedMessagePluginMessagePart5.text, from_msg.text);
        strcpy(cannedMessagePluginMessagePart5.text, from_msg.text);
    }

    if (changed) // If nothing really changed, don't broadcast on the network or write to flash
        service.reloadConfig(); // TODO: does this make sense?
}