#pragma once
#include "SinglePortPlugin.h"
#include "input/InputBroker.h"

enum cannedMessagePluginActionType
{
    CANNED_MESSAGE_ACTION_NONE,
    CANNED_MESSAGE_ACTION_SELECT,
    CANNED_MESSAGE_ACTION_UP,
    CANNED_MESSAGE_ACTION_DOWN
};

enum cannedMessagePluginSendigState
{
    SENDING_STATE_NONE,
    SENDING_STATE_ACTIVE
};


#define CANNED_MESSAGE_PLUGIN_MESSAGE_MAX_COUNT 50
/**
 * Due to config-packet size restrictions we cannot have user configuration bigger
 * than Constants_DATA_PAYLOAD_LEN bytes.
 */
#define CANNED_MESSAGE_PLUGIN_MESSAGES_SIZE 200

class CannedMessagePlugin :
    public SinglePortPlugin,
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
    cannedMessagePluginSendigState getSendingState();
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

    volatile cannedMessagePluginActionType action = CANNED_MESSAGE_ACTION_NONE;
    int currentMessageIndex = -1;
    cannedMessagePluginSendigState sendingState = SENDING_STATE_NONE;

    char messageStore[CANNED_MESSAGE_PLUGIN_MESSAGES_SIZE];
    char *messages[CANNED_MESSAGE_PLUGIN_MESSAGE_MAX_COUNT];
    int messagesCount = 0;
    unsigned long lastTouchMillis = 0;
};

extern CannedMessagePlugin *cannedMessagePlugin;
