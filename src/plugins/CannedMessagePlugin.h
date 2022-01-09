#pragma once
#include "SinglePortPlugin.h"
#include "input/HardwareInput.h"

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

#define CANNED_MESSAGE_PLUGIN_MESSAGE_MAX_LEN 50

static char cannedMessagePluginMessages[][CANNED_MESSAGE_PLUGIN_MESSAGE_MAX_LEN] =
{
    "Need helping hand",
    "Help me with saw",
    "I need an alpinist",
    "I need ambulance",
    "I'm fine",
    "I'm already waiting",
    "I will be late",
    "I couldn't join",
    "We have company"
};

class CannedMessagePlugin :
    public SinglePortPlugin,
    public Observable<const meshtastic::Status *>,
    private concurrency::OSThread
{
    CallbackObserver<CannedMessagePlugin, const InputEvent *> inputObserver =
        CallbackObserver<CannedMessagePlugin, const InputEvent *>(
            this, &CannedMessagePlugin::handleInputEvent);
  public:
    CannedMessagePlugin(
        Observable<const InputEvent *> *input);
    String getCurrentMessage();
    String getPrevMessage();
    String getNextMessage();
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

    int getNextIndex();
    int getPrevIndex();

    int handleInputEvent(const InputEvent *event);

    volatile cannedMessagePluginActionType action = CANNED_MESSAGE_ACTION_NONE;
    int currentMessageIndex = -1;
    cannedMessagePluginSendigState sendingState = SENDING_STATE_NONE;
};

extern CannedMessagePlugin *cannedMessagePlugin;
