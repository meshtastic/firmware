#pragma once

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

/*
 * Radio interface for ExternalNotificationModule
 *
 */
class ExternalNotificationModule : public SinglePortModule, private concurrency::OSThread
{
  uint32_t output = 0;

  public:
    ExternalNotificationModule();

    uint32_t nagCycleCutoff = UINT32_MAX;

    void setExternalOn(uint8_t index = 0);
    void setExternalOff(uint8_t index = 0);
    bool getExternal(uint8_t index = 0);

  protected:
    /** Called to handle a particular incoming message
    @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual ProcessMessage handleReceived(const MeshPacket &mp) override;

    virtual int32_t runOnce() override;
};

extern ExternalNotificationModule *externalNotificationModule;
