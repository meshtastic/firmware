#pragma once
#include "SinglePortPlugin.h"

enum cannedMessagePluginRotatyStateType
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

class CannedMessagePlugin : public SinglePortPlugin, private concurrency::OSThread
{
  public:
    CannedMessagePlugin();
    void select();
    void directionA();
    void directionB();

  protected:

    virtual int32_t runOnce();  

    MeshPacket *preparePacket();

    void sendText(NodeNum dest, bool wantReplies);

    // TODO: make this configurable
    volatile cannedMessagePluginActionType cwRotationMeaning = ACTION_UP;

    bool needSend = false;
    volatile cannedMessagePluginActionType action = ACTION_NONE;
    volatile cannedMessagePluginRotatyStateType rotaryStateCW = EVENT_CLEARED;
    volatile cannedMessagePluginRotatyStateType rotaryStateCCW = EVENT_CLEARED;
    volatile int rotaryLevelA = LOW;
    volatile int rotaryLevelB = LOW;
//    volatile bool enableEvent = true;
};

extern CannedMessagePlugin *cannedMessagePlugin;
