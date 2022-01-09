#include "configuration.h"
#include "CannedMessagePlugin.h"
#include "MeshService.h"
#include "main.h"

#include <assert.h>

CannedMessagePlugin *cannedMessagePlugin;

CannedMessagePlugin::CannedMessagePlugin(
    Observable<const InputEvent *> *input)
    : SinglePortPlugin("canned", PortNum_TEXT_MESSAGE_APP),
    concurrency::OSThread("CannedMessagePlugin")
{
    this->inputObserver.observe(input);
}

int CannedMessagePlugin::handleInputEvent(const InputEvent *event)
{
    bool validEvent = false;
    if (event->inputEvent == INPUT_EVENT_UP)
    {
        this->action = CANNED_MESSAGE_ACTION_UP;
        validEvent = true;
    }
    if (event->inputEvent == INPUT_EVENT_DOWN)
    {
        this->action = CANNED_MESSAGE_ACTION_DOWN;
        validEvent = true;
    }
    if (event->inputEvent == INPUT_EVENT_SELECT)
    {
        this->action = CANNED_MESSAGE_ACTION_SELECT;
        validEvent = true;
    }

    if (validEvent)
    {
        // Let runOnce to be called immediately.
        runned(millis());
        setInterval(0);
    }

    return 0;
}

void CannedMessagePlugin::sendText(NodeNum dest,
      const char* message,
      bool wantReplies)
{
    MeshPacket *p = allocDataPacket();
    p->to = dest;
    p->decoded.payload.size = strlen(message);
    memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);

//    PacketId prevPacketId = p->id; // In case we need it later.

    DEBUG_MSG("Sending message id=%d, msg=%.*s\n",
      p->id, p->decoded.payload.size, p->decoded.payload.bytes);

    service.sendToMesh(p);
}

int32_t CannedMessagePlugin::runOnce()
{
    if (this->sendingState == SENDING_STATE_ACTIVE)
    {
        // TODO: might have some feedback of sendig state
        this->sendingState = SENDING_STATE_NONE;
        this->notifyObservers(NULL);
    }
    else if ((this->action != CANNED_MESSAGE_ACTION_NONE)
        && (this->currentMessageIndex == -1))
    {
        this->currentMessageIndex = 0;
        DEBUG_MSG("First touch.\n");
    }
    else if (this->action == CANNED_MESSAGE_ACTION_SELECT)
    {
        sendText(
            NODENUM_BROADCAST,
            cannedMessagePluginMessages[this->currentMessageIndex],
            true);
        this->sendingState = SENDING_STATE_ACTIVE;
        this->currentMessageIndex = -1;
        return 2000;
    }
    else if (this->action == CANNED_MESSAGE_ACTION_UP)
    {
        this->currentMessageIndex = getPrevIndex();
        DEBUG_MSG("MOVE UP. Current message:%ld\n",
            millis());
    }
    else if (this->action == CANNED_MESSAGE_ACTION_DOWN)
    {
        this->currentMessageIndex = this->getNextIndex();
        DEBUG_MSG("MOVE DOWN. Current message:%ld\n",
            millis());
    }
    if (this->action != CANNED_MESSAGE_ACTION_NONE)
    {
        this->action = CANNED_MESSAGE_ACTION_NONE;
        this->notifyObservers(NULL);
    }

    return 30000;
}

String CannedMessagePlugin::getCurrentMessage()
{
    return cannedMessagePluginMessages[this->currentMessageIndex];
}
String CannedMessagePlugin::getPrevMessage()
{
    return cannedMessagePluginMessages[this->getPrevIndex()];
}
String CannedMessagePlugin::getNextMessage()
{
    return cannedMessagePluginMessages[this->getNextIndex()];
}
bool CannedMessagePlugin::shouldDraw()
{
    return (currentMessageIndex != -1) || (this->sendingState != SENDING_STATE_NONE);
}
cannedMessagePluginSendigState CannedMessagePlugin::getSendingState()
{
    return this->sendingState;
}

int CannedMessagePlugin::getNextIndex()
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

int CannedMessagePlugin::getPrevIndex()
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