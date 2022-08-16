#pragma once
#if HAS_SCREEN
#include "ProtobufModule.h"
#include "input/InputBroker.h"

enum cannedMessageModuleRunState
{
    CANNED_MESSAGE_RUN_STATE_DISABLED,
    CANNED_MESSAGE_RUN_STATE_INACTIVE,
    CANNED_MESSAGE_RUN_STATE_ACTIVE,
    CANNED_MESSAGE_RUN_STATE_SENDING_ACTIVE,
    CANNED_MESSAGE_RUN_STATE_ACTION_SELECT,
    CANNED_MESSAGE_RUN_STATE_ACTION_UP,
    CANNED_MESSAGE_RUN_STATE_ACTION_DOWN,
};


#define CANNED_MESSAGE_MODULE_MESSAGE_MAX_COUNT 50
/**
 * Sum of CannedMessageModuleConfig part sizes.
 */
#define CANNED_MESSAGE_MODULE_MESSAGES_SIZE 800

class CannedMessageModule :
    public SinglePortModule,
    public Observable<const UIFrameEvent *>,
    private concurrency::OSThread
{
    CallbackObserver<CannedMessageModule, const InputEvent *> inputObserver =
        CallbackObserver<CannedMessageModule, const InputEvent *>(
            this, &CannedMessageModule::handleInputEvent);
  public:
    CannedMessageModule();
    const char* getCurrentMessage();
    const char* getPrevMessage();
    const char* getNextMessage();
    bool shouldDraw();
    void eventUp();
    void eventDown();
    void eventSelect();

    void handleGetCannedMessageModuleMessages(const MeshPacket &req, AdminMessage *response);
    void handleSetCannedMessageModuleMessages(const char *from_msg);

  protected:

    virtual int32_t runOnce() override;

    void sendText(
        NodeNum dest,
        const char* message,
        bool wantReplies);

    int splitConfiguredMessages();
    int getNextIndex();
    int getPrevIndex();

    int handleInputEvent(const InputEvent *event);
    virtual bool wantUIFrame() override { return this->shouldDraw(); }
    virtual Observable<const UIFrameEvent *>* getUIFrameObservable() override { return this; }
    virtual void drawFrame(
        OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;
    virtual AdminMessageHandleResult handleAdminMessageForModule(
        const MeshPacket &mp, AdminMessage *request, AdminMessage *response) override;

    void loadProtoForModule();
    bool saveProtoForModule();

    void installDefaultCannedMessageModuleConfig();

    int currentMessageIndex = -1;
    cannedMessageModuleRunState runState = CANNED_MESSAGE_RUN_STATE_INACTIVE;

    char messageStore[CANNED_MESSAGE_MODULE_MESSAGES_SIZE+1];
    char *messages[CANNED_MESSAGE_MODULE_MESSAGE_MAX_COUNT];
    int messagesCount = 0;
    unsigned long lastTouchMillis = 0;
};

extern CannedMessageModule *cannedMessageModule;
#endif