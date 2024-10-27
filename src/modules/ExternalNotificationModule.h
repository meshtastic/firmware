#pragma once

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL) && !defined(CONFIG_IDF_TARGET_ESP32C6)
#include <NonBlockingRtttl.h>
#else
// Noop class for portduino.
class rtttl
{
  public:
    explicit rtttl() {}
    static bool isPlaying() { return false; }
    static void play() {}
    static void begin(byte a, const char *b){};
    static void stop() {}
    static bool done() { return true; }
};
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

    uint32_t nagCycleCutoff = 1;

    void setExternalState(uint8_t index = 0, bool on = false);
    bool getExternal(uint8_t index = 0);

    void setMute(bool mute) { isMuted = mute; }
    bool getMute() { return isMuted; }

    void stopNow();

    void handleGetRingtone(const meshtastic_MeshPacket &req, meshtastic_AdminMessage *response);
    void handleSetRingtone(const char *from_msg);

  protected:
    /** Called to handle a particular incoming message
    @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered for
    it
    */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    virtual int32_t runOnce() override;

    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;

    bool isNagging = false;

    bool isMuted = false;

    virtual AdminMessageHandleResult handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                                 meshtastic_AdminMessage *request,
                                                                 meshtastic_AdminMessage *response) override;
};

extern ExternalNotificationModule *externalNotificationModule;