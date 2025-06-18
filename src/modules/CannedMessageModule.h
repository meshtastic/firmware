#pragma once
#if HAS_SCREEN
#include "ProtobufModule.h"
#include "input/InputBroker.h"

// ============================
//        Enums & Defines
// ============================

enum cannedMessageModuleRunState {
    CANNED_MESSAGE_RUN_STATE_DISABLED,
    CANNED_MESSAGE_RUN_STATE_INACTIVE,
    CANNED_MESSAGE_RUN_STATE_ACTIVE,
    CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE,
    CANNED_MESSAGE_RUN_STATE_ACK_NACK_RECEIVED,
    CANNED_MESSAGE_RUN_STATE_ACTION_SELECT,
    CANNED_MESSAGE_RUN_STATE_ACTION_UP,
    CANNED_MESSAGE_RUN_STATE_ACTION_DOWN,
    CANNED_MESSAGE_RUN_STATE_DESTINATION_SELECTION,
    CANNED_MESSAGE_RUN_STATE_FREETEXT,
    CANNED_MESSAGE_RUN_STATE_MESSAGE_SELECTION,
    CANNED_MESSAGE_RUN_STATE_EMOTE_PICKER
};

enum CannedMessageModuleIconType { shift, backspace, space, enter };

#define CANNED_MESSAGE_MODULE_MESSAGE_MAX_COUNT 50
#define CANNED_MESSAGE_MODULE_MESSAGES_SIZE 800

#ifndef CANNED_MESSAGE_MODULE_ENABLE
#define CANNED_MESSAGE_MODULE_ENABLE 0
#endif

// ============================
//        Data Structures
// ============================

struct Letter {
    String character;
    float width;
    int rectX;
    int rectY;
    int rectWidth;
    int rectHeight;
};

struct NodeEntry {
    meshtastic_NodeInfoLite *node;
    uint32_t lastHeard;
};

// ============================
//      Main Class
// ============================

class CannedMessageModule : public SinglePortModule, public Observable<const UIFrameEvent *>, private concurrency::OSThread
{
  public:
    CannedMessageModule();

    void LaunchWithDestination(NodeNum, uint8_t newChannel = 0);
    void LaunchFreetextWithDestination(NodeNum, uint8_t newChannel = 0);

    // === Emote Picker navigation ===
    int emotePickerIndex = 0; // Tracks currently selected emote in the picker

    // === Message navigation ===
    const char *getCurrentMessage();
    const char *getPrevMessage();
    const char *getNextMessage();
    const char *getMessageByIndex(int index);
    const char *getNodeName(NodeNum node);

    // === State/UI ===
    bool shouldDraw();
    bool hasMessages();
    void showTemporaryMessage(const String &message);
    void resetSearch();
    void updateDestinationSelectionList();
    void drawDestinationSelectionScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    bool isCharInputAllowed() const;
    String drawWithCursor(String text, int cursor);

    // === Emote Picker ===
    int handleEmotePickerInput(const InputEvent *event);
    void drawEmotePickerScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

    // === Admin Handlers ===
    void handleGetCannedMessageModuleMessages(const meshtastic_MeshPacket &req, meshtastic_AdminMessage *response);
    void handleSetCannedMessageModuleMessages(const char *from_msg);

#ifdef RAK14014
    cannedMessageModuleRunState getRunState() const { return runState; }
#endif

    // === Packet Interest Filter ===
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override
    {
        if (p->rx_rssi != 0)
            lastRxRssi = p->rx_rssi;
        if (p->rx_snr > 0)
            lastRxSnr = p->rx_snr;
        return (p->decoded.portnum == meshtastic_PortNum_ROUTING_APP) ? waitingForAck : false;
    }

  protected:
    // === Thread Entry Point ===
    virtual int32_t runOnce() override;

    // === Transmission ===
    void sendText(NodeNum dest, ChannelIndex channel, const char *message, bool wantReplies);
    void drawHeader(OLEDDisplay *display, int16_t x, int16_t y, char *buffer);
    int splitConfiguredMessages();
    int getNextIndex();
    int getPrevIndex();

#if defined(USE_VIRTUAL_KEYBOARD)
    void drawKeyboard(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    String keyForCoordinates(uint x, uint y);
    void drawShiftIcon(OLEDDisplay *display, int x, int y, float scale = 1);
    void drawBackspaceIcon(OLEDDisplay *display, int x, int y, float scale = 1);
    void drawEnterIcon(OLEDDisplay *display, int x, int y, float scale = 1);
#endif

    // === Input Handling ===
    int handleInputEvent(const InputEvent *event);
    virtual bool wantUIFrame() override { return shouldDraw(); }
    virtual Observable<const UIFrameEvent *> *getUIFrameObservable() override { return this; }
    virtual bool interceptingKeyboardInput() override;
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;
    virtual AdminMessageHandleResult handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                                 meshtastic_AdminMessage *request,
                                                                 meshtastic_AdminMessage *response) override;

    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    void loadProtoForModule();
    bool saveProtoForModule();
    void installDefaultCannedMessageModuleConfig();

  private:
    // === Input Observers ===
    CallbackObserver<CannedMessageModule, const InputEvent *> inputObserver =
        CallbackObserver<CannedMessageModule, const InputEvent *>(this, &CannedMessageModule::handleInputEvent);

    // === Display and UI ===
    int displayHeight = 64;
    int destIndex = 0;
    int scrollIndex = 0;
    int visibleRows = 0;
    bool needsUpdate = true;
    unsigned long lastUpdateMillis = 0;
    String searchQuery;
    String freetext;
    String temporaryMessage;

    // === Message Storage ===
    char messageStore[CANNED_MESSAGE_MODULE_MESSAGES_SIZE + 1];
    char *messages[CANNED_MESSAGE_MODULE_MESSAGE_MAX_COUNT];
    int messagesCount = 0;
    int currentMessageIndex = -1;

    // === Routing & Acknowledgment ===
    NodeNum dest = NODENUM_BROADCAST;     // Destination node for outgoing messages (default: broadcast)
    NodeNum incoming = NODENUM_BROADCAST; // Source node from which last ACK/NACK was received
    NodeNum lastSentNode = 0;             // Tracks the most recent node we sent a message to (for UI display)
    ChannelIndex channel = 0;             // Channel index used when sending a message

    bool ack = false;               // True = ACK received, False = NACK or failed
    bool waitingForAck = false;     // True if we're expecting an ACK and should monitor routing packets
    bool lastAckWasRelayed = false; // True if the ACK was relayed through intermediate nodes
    uint8_t lastAckHopStart = 0;    // Hop start value from the received ACK packet
    uint8_t lastAckHopLimit = 0;    // Hop limit value from the received ACK packet

    float lastRxSnr = 0;    // SNR from last received ACK (used for diagnostics/UI)
    int32_t lastRxRssi = 0; // RSSI from last received ACK (used for diagnostics/UI)

    // === State Tracking ===
    cannedMessageModuleRunState runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;
    char highlight = 0x00;
    char payload = 0x00;
    unsigned int cursor = 0;
    unsigned long lastTouchMillis = 0;
    uint32_t lastFilterUpdate = 0;
    static constexpr uint32_t filterDebounceMs = 30;
    std::vector<uint8_t> activeChannelIndices;
    std::vector<NodeEntry> filteredNodes;

#if defined(USE_VIRTUAL_KEYBOARD)
    bool shift = false;
    int charSet = 0; // 0=ABC, 1=123
#endif

    bool isUpEvent(const InputEvent *event);
    bool isDownEvent(const InputEvent *event);
    bool isSelectEvent(const InputEvent *event);
    bool handleTabSwitch(const InputEvent *event);
    int handleDestinationSelectionInput(const InputEvent *event, bool isUp, bool isDown, bool isSelect);
    bool handleMessageSelectorInput(const InputEvent *event, bool isUp, bool isDown, bool isSelect);
    bool handleFreeTextInput(const InputEvent *event);

#if defined(USE_VIRTUAL_KEYBOARD)
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
