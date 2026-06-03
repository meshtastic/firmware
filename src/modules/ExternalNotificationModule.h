#pragma once

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "input/InputBroker.h"

#ifdef HAS_RGB_LED
#include "AmbientLightingThread.h"
extern AmbientLightingThread *ambientLightingThread;
#endif

// Drive a single WS2812 as the notification LED (M1/M2-style LED_NOTIFICATION
// but addressable). A variant defines NEOPIXEL_STATUS_NOTIFICATION_PIN to
// enable. Colour defaults to green but can be overridden.
#ifdef NEOPIXEL_STATUS_NOTIFICATION_PIN
#include <Adafruit_NeoPixel.h>
#ifndef NEOPIXEL_STATUS_TYPE
#define NEOPIXEL_STATUS_TYPE (NEO_GRB + NEO_KHZ800)
#endif
#ifndef NEOPIXEL_STATUS_NOTIFICATION_COLOR
#define NEOPIXEL_STATUS_NOTIFICATION_COLOR 0x00FF00 // green
#endif
#endif

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
    CallbackObserver<ExternalNotificationModule, const InputEvent *> inputObserver =
        CallbackObserver<ExternalNotificationModule, const InputEvent *>(this, &ExternalNotificationModule::handleInputEvent);
    uint32_t output = 0;

#ifdef NEOPIXEL_STATUS_NOTIFICATION_PIN
    Adafruit_NeoPixel notificationPixel = Adafruit_NeoPixel(1, NEOPIXEL_STATUS_NOTIFICATION_PIN, NEOPIXEL_STATUS_TYPE);
#endif

  public:
    ExternalNotificationModule();

    int handleInputEvent(const InputEvent *arg);

    uint32_t nagCycleCutoff = 1;

    void setExternalState(uint8_t index = 0, bool on = false);
    bool getExternal(uint8_t index = 0);

    void setMute(bool mute) { isSilenced = mute; }
    bool getMute() { return isSilenced; }

    bool canBuzz();
    bool nagging();

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

    bool isSilenced = false;

    virtual AdminMessageHandleResult handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                                 meshtastic_AdminMessage *request,
                                                                 meshtastic_AdminMessage *response) override;
};

extern ExternalNotificationModule *externalNotificationModule;