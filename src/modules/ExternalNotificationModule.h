#pragma once

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#ifndef ARCH_PORTDUINO
#include <NonBlockingRtttl.h>
#endif
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

    void stopNow();

    char pwmRingtone[Constants_DATA_PAYLOAD_LEN] = "a:d=8,o=5,b=125:4d#6,a#,2d#6,16p,g#,4a#,4d#.,p,16g,16a#,d#6,a#,f6,2d#6,16p,c#.6,16c6,16a#,g#.,2a#";

  protected:
    /** Called to handle a particular incoming message
    @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual ProcessMessage handleReceived(const MeshPacket &mp) override;

    virtual int32_t runOnce() override;
};

extern ExternalNotificationModule *externalNotificationModule;
