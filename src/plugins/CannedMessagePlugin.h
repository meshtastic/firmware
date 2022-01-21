#pragma once
#include "ProtobufPlugin.h"
#include "input/InputBroker.h"

enum cannedMessagePluginRunState
{
    CANNED_MESSAGE_RUN_STATE_INACTIVE,
    CANNED_MESSAGE_RUN_STATE_ACTIVE,
    CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE,
    CANNED_MESSAGE_RUN_STATE_ACTION_SELECT,
    CANNED_MESSAGE_RUN_STATE_ACTION_UP,
    CANNED_MESSAGE_RUN_STATE_ACTION_DOWN
};


#define CANNED_MESSAGE_PLUGIN_MESSAGE_MAX_COUNT 50
/**
 * Due to config-packet size restrictions we cannot have user configuration bigger
 * than Constants_DATA_PAYLOAD_LEN bytes.
 */
#define CANNED_MESSAGE_PLUGIN_MESSAGES_SIZE 1002

class CannedMessagePlugin :
    public ProtobufPlugin<AdminMessage>,
    public Observable<const UIFrameEvent *>,
    private concurrency::OSThread
{
    CallbackObserver<CannedMessagePlugin, const InputEvent *> inputObserver =
        CallbackObserver<CannedMessagePlugin, const InputEvent *>(
            this, &CannedMessagePlugin::handleInputEvent);
  public:
    CannedMessagePlugin();
    const char* getCurrentMessage();
    const char* getPrevMessage();
    const char* getNextMessage();
    bool shouldDraw();
    void eventUp();
    void eventDown();
    void eventSelect();

  protected:

    virtual int32_t runOnce();

    void sendText(
        NodeNum dest,
        const char* message,
        bool wantReplies);

    int splitConfiguredMessages();
    int getNextIndex();
    int getPrevIndex();

    int handleInputEvent(const InputEvent *event);
    virtual bool wantUIFrame() { return this->shouldDraw(); }
    virtual Observable<const UIFrameEvent *>* getUIFrameObservable() { return this; }
    virtual void drawFrame(
        OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    virtual bool handleAdminMessageForPlugin(const MeshPacket &mp, AdminMessage *r);
    virtual bool handleReceivedProtobuf(const MeshPacket &mp, AdminMessage *p)
    {
        // TODO: this method might be used instead of handleAdminMessageForPlugin() 
        return false;
    };

    void loadProtoForPlugin();
    bool saveProtoForPlugin();
    void installProtoDefaultsForPlugin();

    void handleGetCannedMessagePluginPart1(const MeshPacket &req);
    void handleGetCannedMessagePluginPart2(const MeshPacket &req);
    void handleGetCannedMessagePluginPart3(const MeshPacket &req);
    void handleGetCannedMessagePluginPart4(const MeshPacket &req);
    void handleGetCannedMessagePluginPart5(const MeshPacket &req);

    void handleSetCannedMessagePluginPart1(const CannedMessagePluginMessagePart1 &from_msg);
    void handleSetCannedMessagePluginPart2(const CannedMessagePluginMessagePart2 &from_msg);
    void handleSetCannedMessagePluginPart3(const CannedMessagePluginMessagePart3 &from_msg);
    void handleSetCannedMessagePluginPart4(const CannedMessagePluginMessagePart4 &from_msg);
    void handleSetCannedMessagePluginPart5(const CannedMessagePluginMessagePart5 &from_msg);

    void installDefaultCannedMessagePluginPart1();
    void installDefaultCannedMessagePluginPart2();
    void installDefaultCannedMessagePluginPart3();
    void installDefaultCannedMessagePluginPart4();
    void installDefaultCannedMessagePluginPart5();

    int currentMessageIndex = -1;
    cannedMessagePluginRunState runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;

    char messageStore[CANNED_MESSAGE_PLUGIN_MESSAGES_SIZE];
    char *messages[CANNED_MESSAGE_PLUGIN_MESSAGE_MAX_COUNT];
    int messagesCount = 0;
    unsigned long lastTouchMillis = 0;
};

extern CannedMessagePlugin *cannedMessagePlugin;
