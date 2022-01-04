#pragma once
#include "SinglePortPlugin.h"

enum cannedMessagePluginRotaryStateType
{
    EVENT_OCCURRED,
    EVENT_CLEARED
};

enum cannedMessagePluginActionType
{
    ACTION_NONE,
    ACTION_PRESSED,
    ACTION_UP,
    ACTION_DOWN
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
  public:
    CannedMessagePlugin();
    void select();
    void directionA();
    void directionB();
    String getCurrentMessage()
    {
        return cannedMessagePluginMessages[this->currentMessageIndex];
    }
    String getPrevMessage()
    {
        return cannedMessagePluginMessages[this->getPrevIndex()];
    }
    String getNextMessage()
    {
        return cannedMessagePluginMessages[this->getNextIndex()];
    }
    bool shouldDraw()
    {
        return (currentMessageIndex != -1) || (this->sendingState != SENDING_STATE_NONE);
    }
    cannedMessagePluginSendigState getSendingState()
    {
        return this->sendingState;
    }

  protected:

    virtual int32_t runOnce();  

    void sendText(
        NodeNum dest,
        const char* message,
        bool wantReplies);

    int getNextIndex()
    {
        if (this->currentMessageIndex >=
            (sizeof(cannedMessagePluginMessages) / CANNED_MESSAGE_PLUGIN_MESSAGE_MAX_LEN) - 1)
        {
            return 0;
        }
        else
        {
            return this->currentMessageIndex + 1;
        }
    }

    int getPrevIndex()
    {
        if (this->currentMessageIndex <= 0)
        {
            return
             sizeof(cannedMessagePluginMessages) / CANNED_MESSAGE_PLUGIN_MESSAGE_MAX_LEN - 1;
        }
        else
        {
            return this->currentMessageIndex - 1;
        }
    }

    // TODO: make this configurable
    volatile cannedMessagePluginActionType cwRotationMeaning = ACTION_UP;

    volatile cannedMessagePluginActionType action = ACTION_NONE;
    volatile cannedMessagePluginRotaryStateType rotaryStateCW = EVENT_CLEARED;
    volatile cannedMessagePluginRotaryStateType rotaryStateCCW = EVENT_CLEARED;
    volatile int rotaryLevelA = LOW;
    volatile int rotaryLevelB = LOW;
    int currentMessageIndex = -1;
    cannedMessagePluginSendigState sendingState = SENDING_STATE_NONE;
};

extern CannedMessagePlugin *cannedMessagePlugin;
