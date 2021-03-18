#pragma once

#include "SinglePortPlugin.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

/*
 * Radio interface for ExternalNotificationPlugin
 *
 */
class ExternalNotificationPlugin : public SinglePortPlugin, private concurrency::OSThread
{
  public:
    ExternalNotificationPlugin();

    void setExternalOn();
    void setExternalOff();
    void getExternal();

  protected:
    // virtual MeshPacket *allocReply();

    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceived(const MeshPacket &mp);

    virtual int32_t runOnce();
};
