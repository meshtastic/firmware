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

#if !MESHTASTIC_EXCLUDE_RTTTL
#include <NonBlockingRtttl.h>
#else
// Noop class for portduino/STM32WL/ESP32C6 - none can drive PWM RTTTL playback.
class rtttl
{
  public:
    explicit rtttl() {}
    static bool isPlaying() { return false; }
    static void play() {}
    static void begin(byte a, const char *b) {};
    static void stop() {}
    static bool done() { return true; }
};
#endif
#include <Arduino.h>
#include <functional>

#if HAS_LIBNOTIFY
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#endif

/*
 * Radio interface for ExternalNotificationModule
 *
 */
class ExternalNotificationModule : public SinglePortModule, private concurrency::OSThread
{
#if !MESHTASTIC_EXCLUDE_INPUTBROKER
    CallbackObserver<ExternalNotificationModule, const InputEvent *> inputObserver =
        CallbackObserver<ExternalNotificationModule, const InputEvent *>(this, &ExternalNotificationModule::handleInputEvent);
#endif
    uint32_t output = 0;

#ifdef NEOPIXEL_STATUS_NOTIFICATION_PIN
    Adafruit_NeoPixel notificationPixel = Adafruit_NeoPixel(1, NEOPIXEL_STATUS_NOTIFICATION_PIN, NEOPIXEL_STATUS_TYPE);
#endif

  public:
    ExternalNotificationModule();
#if HAS_LIBNOTIFY
    ~ExternalNotificationModule();
#endif

#if !MESHTASTIC_EXCLUDE_INPUTBROKER
    int handleInputEvent(const InputEvent *arg);
#endif

    /// When the current nag cycle ends. Meaningful only while isNagging is set; never test it for a
    /// magic value.
    uint32_t nagCycleCutoff = 0;

    void setExternalState(uint8_t index = 0, bool on = false);
    bool getExternal(uint8_t index = 0);

    void setMute(bool mute) { isSilenced = mute; }
    bool getMute() { return isSilenced; }

    bool canBuzz();
    bool nagging();

    void stopNow();

    // Fire the configured message outputs for a non-message event such as a geofence crossing.
    void startNotification();

#if !MESHTASTIC_EXCLUDE_RTTTL
    void handleGetRingtone(const meshtastic_MeshPacket &req, meshtastic_AdminMessage *response);
    void handleSetRingtone(const char *from_msg);
#endif

  protected:
    /** Called to handle a particular incoming message
    @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered for
    it
    */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    virtual int32_t runOnce() override;

    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;

    // Drive the configured buzzer output (I2S, PWM ringtone, or plain GPIO).
    void triggerBuzzerOutput();
    void triggerVibraOutput();
    void armNagCycle();

    bool isNagging = false;

    bool isSilenced = false;
    bool buzzerShouldAlert = false;

    virtual AdminMessageHandleResult handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                                 meshtastic_AdminMessage *request,
                                                                 meshtastic_AdminMessage *response) override;

#if HAS_LIBNOTIFY
    /// Resolve the sender/body on the caller's thread and hand the notification to notifyWorker().
    void portduinoNotify(const meshtastic_MeshPacket &mp);

    /// Drains notifyQueue. Owns every libnotify call: notify_notification_show() is a synchronous
    /// DBus round trip, and meshtasticd's packet path is single-threaded, so a slow or wedged
    /// notification daemon would otherwise stall packet handling.
    void notifyWorker();

    /// Emit whatever state change notifyWorker() recorded, on the calling (main) thread.
    void reportNotifyStatus();

    std::thread notifyThread;
    std::mutex notifyLock;
    std::condition_variable notifyWake;
    std::deque<std::pair<std::string, std::string>> notifyQueue; // <summary, body>, guarded by notifyLock

    /// Set only by the destructor, and the worker's only exit path. Kept separate from the backoff
    /// state below so a failing notification daemon can never end the thread.
    bool notifyShutdown = false;

    /// Backoff window. notifyRetryAfter is read only while notifyRetryArmed is set, so it reserves
    /// no sentinel value of its own. Guarded by notifyLock.
    bool notifyRetryArmed = false;
    uint32_t notifyRetryAfter = 0;
    uint32_t notifyBackoffMs = 0;

    /// A state change the worker recorded for the main thread to log. The worker must not call the
    /// LOG_ macros itself: RedirectablePrint formats into a shared static buffer that nothing
    /// guards, and every other writer to it is on the main thread.
    struct NotifyStatus {
        bool pending = false;   // there is something to report
        bool recovered = false; // false: started failing; true: working again
        uint32_t retryInMs = 0;
        char reason[128] = {};
    };
    NotifyStatus notifyStatus; // guarded by notifyLock
#endif
};

extern ExternalNotificationModule *externalNotificationModule;
