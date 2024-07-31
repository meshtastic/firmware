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
    CANNED_MESSAGE_RUN_STATE_MESSAGE,
    CANNED_MESSAGE_RUN_STATE_ACTION_SELECT,
    CANNED_MESSAGE_RUN_STATE_ACTION_UP,
    CANNED_MESSAGE_RUN_STATE_ACTION_DOWN,
};

enum cannedMessageDestinationType {
    CANNED_MESSAGE_DESTINATION_TYPE_NONE,
    CANNED_MESSAGE_DESTINATION_TYPE_NODE,
    CANNED_MESSAGE_DESTINATION_TYPE_CHANNEL
};

enum CannedMessageModuleIconType { shift, backspace, space, enter };

struct Letter {
    String character;
    float width;
    int rectX;
    int rectY;
    int rectWidth;
    int rectHeight;
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

    void showTemporaryMessage(const String &message);

    String drawWithCursor(String text, int cursor);

    /*
      -Override the wantPacket method. We need the Routing Messages to look for ACKs.
    */
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override
    {
        if (p->rx_rssi != 0) {
            this->lastRxRssi = p->rx_rssi;
        }

        if (p->rx_snr > 0) {
            this->lastRxSnr = p->rx_snr;
        }

        switch (p->decoded.portnum) {
        case meshtastic_PortNum_ROUTING_APP:
            return waitingForAck;
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

#if defined(T_WATCH_S3) || defined(RAK14014)
    void drawKeyboard(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    String keyForCoordinates(uint x, uint y);
    bool shift = false;
    int charSet = 0;
    void drawShiftIcon(OLEDDisplay *display, int x, int y, float scale = 1);
    void drawBackspaceIcon(OLEDDisplay *display, int x, int y, float scale = 1);
    void drawEnterIcon(OLEDDisplay *display, int x, int y, float scale = 1);
#endif

    char highlight = 0x00;

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
    bool ack = false;           // True means ACK, false means NAK (error_reason != NONE)
    bool waitingForAck = false; // Are currently interested in routing packets?
    float lastRxSnr = 0;
    int32_t lastRxRssi = 0;

    char messageStore[CANNED_MESSAGE_MODULE_MESSAGES_SIZE + 1];
    char *messages[CANNED_MESSAGE_MODULE_MESSAGE_MAX_COUNT];
    int messagesCount = 0;
    unsigned long lastTouchMillis = 0;
    String temporaryMessage;

#if defined(T_WATCH_S3) || defined(RAK14014)
    Letter keyboard[2][4][10] = {{{{"Q", 20, 0, 0, 0, 0},
                                   {"W", 22, 0, 0, 0, 0},
                                   {"E", 17, 0, 0, 0, 0},
                                   {"R", 16.5, 0, 0, 0, 0},
                                   {"T", 14, 0, 0, 0, 0},
                                   {"Y", 15, 0, 0, 0, 0},
                                   {"U", 16.5, 0, 0, 0, 0},
                                   {"I", 5, 0, 0, 0, 0},
                                   {"O", 19.5, 0, 0, 0, 0},
                                   {"P", 15.5, 0, 0, 0, 0}},
                                  {{"A", 14, 0, 0, 0, 0},
                                   {"S", 15, 0, 0, 0, 0},
                                   {"D", 16.5, 0, 0, 0, 0},
                                   {"F", 15, 0, 0, 0, 0},
                                   {"G", 17, 0, 0, 0, 0},
                                   {"H", 15.5, 0, 0, 0, 0},
                                   {"J", 12, 0, 0, 0, 0},
                                   {"K", 15.5, 0, 0, 0, 0},
                                   {"L", 14, 0, 0, 0, 0},
                                   {"", 0, 0, 0, 0, 0}},
                                  {{"⇧", 20, 0, 0, 0, 0},
                                   {"Z", 14, 0, 0, 0, 0},
                                   {"X", 14.5, 0, 0, 0, 0},
                                   {"C", 15.5, 0, 0, 0, 0},
                                   {"V", 13.5, 0, 0, 0, 0},
                                   {"B", 15, 0, 0, 0, 0},
                                   {"N", 15, 0, 0, 0, 0},
                                   {"M", 17, 0, 0, 0, 0},
                                   {"⌫", 20, 0, 0, 0, 0},
                                   {"", 0, 0, 0, 0, 0}},
                                  {{"123", 42, 0, 0, 0, 0},
                                   {" ", 64, 0, 0, 0, 0},
                                   {"↵", 36, 0, 0, 0, 0},
                                   {"", 0, 0, 0, 0, 0},
                                   {"", 0, 0, 0, 0, 0},
                                   {"", 0, 0, 0, 0, 0},
                                   {"", 0, 0, 0, 0, 0},
                                   {"", 0, 0, 0, 0, 0},
                                   {"", 0, 0, 0, 0, 0},
                                   {"", 0, 0, 0, 0, 0}}},
                                 {{{"1", 12, 0, 0, 0, 0},
                                   {"2", 13.5, 0, 0, 0, 0},
                                   {"3", 12.5, 0, 0, 0, 0},
                                   {"4", 14, 0, 0, 0, 0},
                                   {"5", 14, 0, 0, 0, 0},
                                   {"6", 14, 0, 0, 0, 0},
                                   {"7", 13.5, 0, 0, 0, 0},
                                   {"8", 14, 0, 0, 0, 0},
                                   {"9", 14, 0, 0, 0, 0},
                                   {"0", 14, 0, 0, 0, 0}},
                                  {{"-", 8, 0, 0, 0, 0},
                                   {"/", 8, 0, 0, 0, 0},
                                   {":", 4.5, 0, 0, 0, 0},
                                   {";", 4.5, 0, 0, 0, 0},
                                   {"(", 7, 0, 0, 0, 0},
                                   {")", 6.5, 0, 0, 0, 0},
                                   {"$", 12.5, 0, 0, 0, 0},
                                   {"&", 15, 0, 0, 0, 0},
                                   {"@", 21.5, 0, 0, 0, 0},
                                   {"\"", 8, 0, 0, 0, 0}},
                                  {{".", 8, 0, 0, 0, 0},
                                   {",", 8, 0, 0, 0, 0},
                                   {"?", 10, 0, 0, 0, 0},
                                   {"!", 10, 0, 0, 0, 0},
                                   {"'", 10, 0, 0, 0, 0},
                                   {"⌫", 20, 0, 0, 0, 0}},
                                  {{"ABC", 50, 0, 0, 0, 0}, {" ", 64, 0, 0, 0, 0}, {"↵", 36, 0, 0, 0, 0}}}};
#endif
};

extern CannedMessageModule *cannedMessageModule;
#endif
