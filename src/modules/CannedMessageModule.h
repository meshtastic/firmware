#pragma once
#if HAS_SCREEN
#include "ProtobufModule.h"
#include "input/InputBroker.h"

enum cannedMessageModuleRunState {
    CANNED_MESSAGE_RUN_STATE_DISABLED,
    CANNED_MESSAGE_RUN_STATE_INACTIVE,
    CANNED_MESSAGE_RUN_STATE_ACTIVE,
    CANNED_MESSAGE_RUN_STATE_FREETEXT,
    CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE,
    CANNED_MESSAGE_RUN_STATE_ACK_NACK_RECEIVED,
    CANNED_MESSAGE_RUN_STATE_ACTION_SELECT,
    CANNED_MESSAGE_RUN_STATE_ACTION_UP,
    CANNED_MESSAGE_RUN_STATE_ACTION_DOWN,
};

enum cannedMessageDestinationType {
    CANNED_MESSAGE_DESTINATION_TYPE_NONE,
    CANNED_MESSAGE_DESTINATION_TYPE_NODE,
    CANNED_MESSAGE_DESTINATION_TYPE_CHANNEL
};

#define CANNED_MESSAGE_MODULE_MESSAGE_MAX_COUNT 50
/**
 * Sum of CannedMessageModuleConfig part sizes.
 */
#define CANNED_MESSAGE_MODULE_MESSAGES_SIZE 800

#ifndef CANNED_MESSAGE_MODULE_ENABLE
#define CANNED_MESSAGE_MODULE_ENABLE 0
#endif

class CannedMessageModule : public SinglePortModule, public Observable<const UIFrameEvent *>, private concurrency::OSThread
{
    CallbackObserver<CannedMessageModule, const InputEvent *> inputObserver =
        CallbackObserver<CannedMessageModule, const InputEvent *>(this, &CannedMessageModule::handleInputEvent);

  public:
    CannedMessageModule();
    const char *getCurrentMessage();
    const char *getPrevMessage();
    const char *getNextMessage();
    const char *getMessageByIndex(int index);
    const char *getNodeName(NodeNum node);
    bool shouldDraw();
    // void eventUp();
    // void eventDown();
    // void eventSelect();

    void handleGetCannedMessageModuleMessages(const meshtastic_MeshPacket &req, meshtastic_AdminMessage *response);
    void handleSetCannedMessageModuleMessages(const char *from_msg);

    String drawWithCursor(String text, int cursor);

    /*
      -Override the wantPacket method. We need the Routing Messages to look for ACKs.
    */
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override
    {
        switch (p->decoded.portnum) {
        case meshtastic_PortNum_TEXT_MESSAGE_APP:
        case meshtastic_PortNum_ROUTING_APP:
            return true;
        default:
            return false;
        }
    }

  protected:
    virtual int32_t runOnce() override;

    void sendText(NodeNum dest, ChannelIndex channel, const char *message, bool wantReplies);

    int splitConfiguredMessages();
    int getNextIndex();
    int getPrevIndex();

    int handleInputEvent(const InputEvent *event);
    virtual bool wantUIFrame() override { return this->shouldDraw(); }
    virtual Observable<const UIFrameEvent *> *getUIFrameObservable() override { return this; }
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;
    virtual AdminMessageHandleResult handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                                 meshtastic_AdminMessage *request,
                                                                 meshtastic_AdminMessage *response) override;

    /** Called to handle a particular incoming message
     * @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered
     * for it
     */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    void loadProtoForModule();
    bool saveProtoForModule();

    void installDefaultCannedMessageModuleConfig();

    int currentMessageIndex = -1;
    cannedMessageModuleRunState runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
    char payload = 0x00;
    unsigned int cursor = 0;
    String freetext = ""; // Text Buffer for Freetext Editor
    NodeNum dest = NODENUM_BROADCAST;
    ChannelIndex channel = 0;
    cannedMessageDestinationType destSelect = CANNED_MESSAGE_DESTINATION_TYPE_NONE;
    uint8_t numChannels = 0;
    ChannelIndex indexChannels[MAX_NUM_CHANNELS] = {0};
    NodeNum incoming = NODENUM_BROADCAST;
    bool ack = false; // True means ACK, false means NAK (error_reason != NONE)

    char messageStore[CANNED_MESSAGE_MODULE_MESSAGES_SIZE + 1];
    char *messages[CANNED_MESSAGE_MODULE_MESSAGE_MAX_COUNT];
    int messagesCount = 0;
    unsigned long lastTouchMillis = 0;
};

extern CannedMessageModule *cannedMessageModule;
#endif