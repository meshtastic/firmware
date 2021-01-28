#pragma once

#include "SinglePortPlugin.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>


class ExternalNotificationPlugin : private concurrency::OSThread
{
    bool firstTime = 1;
    bool externalCurrentState = 0;

  public:
    ExternalNotificationPlugin();

    void setExternalOn();
    void setExternalOff();
    void getExternal();

  protected:
    virtual int32_t runOnce();
};

extern ExternalNotificationPlugin *externalNotificationPlugin;

/*
 * Radio interface for ExternalNotificationPlugin
 *
 */
class ExternalNotificationPluginRadio : public SinglePortPlugin
{

  public:
    ExternalNotificationPluginRadio() : SinglePortPlugin("ExternalNotificationPluginRadio", PortNum_TEXT_MESSAGE_APP) {}

  protected:
    virtual MeshPacket *allocReply();

    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceived(const MeshPacket &mp);
};

extern ExternalNotificationPluginRadio *externalNotificationPluginRadio;