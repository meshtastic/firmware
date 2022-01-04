#include "configuration.h"
#include "CannedMessagePlugin.h"
#include "MeshService.h"
#include "main.h"

#include <assert.h>

#define PIN_PUSH 21
#define PIN_A    22
#define PIN_B    23

// TODO: add UP-DOWN mode
#define ROTARY_MODE

CannedMessagePlugin *cannedMessagePlugin;

void IRAM_ATTR EXT_INT_PUSH()
{
  cannedMessagePlugin->select();
}

void IRAM_ATTR EXT_INT_DIRECTION_A()
{
  cannedMessagePlugin->directionA();
}

void IRAM_ATTR EXT_INT_DIRECTION_B()
{
  cannedMessagePlugin->directionB();
}

CannedMessagePlugin::CannedMessagePlugin()
    : SinglePortPlugin("canned", PortNum_TEXT_MESSAGE_APP), concurrency::OSThread("CannedMessagePlugin")
{
    // TODO: make pins configurable
    pinMode(PIN_PUSH, INPUT_PULLUP);
    pinMode(PIN_A, INPUT_PULLUP);
    pinMode(PIN_B, INPUT_PULLUP);
    attachInterrupt(PIN_PUSH, EXT_INT_PUSH, RISING);
#ifdef ROTARY_MODE
    attachInterrupt(PIN_A, EXT_INT_DIRECTION_A, CHANGE);
    attachInterrupt(PIN_B, EXT_INT_DIRECTION_B, CHANGE);
    this->rotaryLevelA = digitalRead(PIN_A);
    this->rotaryLevelB = digitalRead(PIN_B);
#endif
}

void CannedMessagePlugin::sendText(NodeNum dest, bool wantReplies)
{
    MeshPacket *p = allocDataPacket();
    p->to = dest;
    const char *replyStr = "This is a canned message";
    p->decoded.payload.size = strlen(replyStr); // You must specify how many bytes are in the reply
    memcpy(p->decoded.payload.bytes, replyStr, p->decoded.payload.size);

//    PacketId prevPacketId = p->id; // In case we need it later.

    DEBUG_MSG("Sending message id=%d, msg=%.*s\n",
      p->id, p->decoded.payload.size, p->decoded.payload.bytes);

    service.sendToMesh(p);
}

int32_t CannedMessagePlugin::runOnce()
{
    /*
    if (this->action == ACTION_PRESSED)
    {
        sendText(NODENUM_BROADCAST, true);
        needSend = false;
    }
    */
    if (this->action == ACTION_PRESSED)
    {
        DEBUG_MSG("SELECTED\n");
    }
    else if (this->action == ACTION_UP)
    {
        DEBUG_MSG("MOVE UP\n");
    }
    else if (this->action == ACTION_DOWN)
    {
        DEBUG_MSG("MOVE_DOWN\n");
    }
    this->action = ACTION_NONE;
    
    return UINT32_MAX;
}

void CannedMessagePlugin::select()
{
    this->action = ACTION_PRESSED;
    setInterval(20);
}

/**
 * @brief Rotary action implementation.
 *   We assume, the following pin setup:
 *    A   --||
 *    GND --||]======== 
 *    B   --||
 * 
 * @return The new level of the actual pin (that is actualPinCurrentLevel).
 */
void CannedMessagePlugin::directionA()
{
#ifdef ROTARY_MODE
    // CW rotation (at least on most common rotary encoders)
    int currentLevelA = digitalRead(PIN_A);
    if (this->rotaryLevelA == currentLevelA)
    {
        return;
    }
    this->rotaryLevelA = currentLevelA;
    bool pinARaising = currentLevelA == HIGH;
    if (pinARaising && (this->rotaryLevelB == LOW))
    {
        if (this->rotaryStateCCW == EVENT_CLEARED)
        {
            this->rotaryStateCCW = EVENT_OCCURRED;
            if ((this->action == ACTION_NONE)
                || (this->action == (cwRotationMeaning == ACTION_UP ? ACTION_UP : ACTION_DOWN)))
            {
                this->action = cwRotationMeaning == ACTION_UP ? ACTION_DOWN : ACTION_UP;
            }
        }
    }
    else if (!pinARaising && (this->rotaryLevelB == HIGH))
    {
        // Logic to prevent bouncing.
        this->rotaryStateCCW = EVENT_CLEARED;
    }
#endif
    setInterval(30);
}

void CannedMessagePlugin::directionB()
{
#ifdef ROTARY_MODE
    // CW rotation (at least on most common rotary encoders)
    int currentLevelB = digitalRead(PIN_B);
    if (this->rotaryLevelB == currentLevelB)
    {
        return;
    }
    this->rotaryLevelB = currentLevelB;
    bool pinBRaising = currentLevelB == HIGH;
    if (pinBRaising && (this->rotaryLevelA == LOW))
    {
        if (this->rotaryStateCW == EVENT_CLEARED)
        {
            this->rotaryStateCW = EVENT_OCCURRED;
            if ((this->action == ACTION_NONE)
                || (this->action == (cwRotationMeaning == ACTION_UP ? ACTION_DOWN : ACTION_UP)))
            {
                this->action = cwRotationMeaning == ACTION_UP ? ACTION_UP : ACTION_DOWN;
            }
        }
    }
    else if (!pinBRaising && (this->rotaryLevelA == HIGH))
    {
        // Logic to prevent bouncing.
        this->rotaryStateCW = EVENT_CLEARED;
    }
#endif
    setInterval(30);
}
