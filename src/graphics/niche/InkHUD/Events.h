#ifdef MESHTASTIC_INCLUDE_INKHUD

#pragma once

/*

Handles non-specific events for InkHUD

Individual applets are responsible for listening for their own events via the module api etc,
however this class handles general events which concern InkHUD as a whole, e.g. shutdown

*/

#include "configuration.h"

#include "Observer.h"

#include "./InkHUD.h"
#include "./Persistence.h"

namespace NicheGraphics::InkHUD
{

class Events
{
  public:
    Events();
    void begin();

    void onButtonShort(); // User button: short press
    void onButtonLong();  // User button: long press

    int beforeDeepSleep(void *unused);                             // Prepare for shutdown
    int beforeReboot(void *unused);                                // Prepare for reboot
    int onReceiveTextMessage(const meshtastic_MeshPacket *packet); // Store most recent text message
    int onAdminMessage(const meshtastic_AdminMessage *message);    // Handle incoming admin messages
#ifdef ARCH_ESP32
    int beforeLightSleep(void *unused); // Prepare for light sleep
#endif

  private:
    // For convenience
    InkHUD *inkhud = nullptr;
    Persistence::Settings *settings = nullptr;

    // Get notified when the system is shutting down
    CallbackObserver<Events, void *> deepSleepObserver = CallbackObserver<Events, void *>(this, &Events::beforeDeepSleep);

    // Get notified when the system is rebooting
    CallbackObserver<Events, void *> rebootObserver = CallbackObserver<Events, void *>(this, &Events::beforeReboot);

    // Cache *incoming* text messages, for use by applets
    CallbackObserver<Events, const meshtastic_MeshPacket *> textMessageObserver =
        CallbackObserver<Events, const meshtastic_MeshPacket *>(this, &Events::onReceiveTextMessage);

    // Get notified of incoming admin messages, and handle any which are relevant to InkHUD
    CallbackObserver<Events, const meshtastic_AdminMessage *> adminMessageObserver =
        CallbackObserver<Events, const meshtastic_AdminMessage *>(this, &Events::onAdminMessage);

#ifdef ARCH_ESP32
    // Get notified when the system is entering light sleep
    CallbackObserver<Events, void *> lightSleepObserver = CallbackObserver<Events, void *>(this, &Events::beforeLightSleep);
#endif

    // If set, InkHUD's data will be erased during onReboot
    bool eraseOnReboot = false;
};

} // namespace NicheGraphics::InkHUD

#endif