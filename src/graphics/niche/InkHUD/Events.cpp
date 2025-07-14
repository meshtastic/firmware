#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./Events.h"

#include "RTC.h"
#include "buzz.h"
#include "modules/ExternalNotificationModule.h"
#include "modules/TextMessageModule.h"
#include "sleep.h"

#include "./Applet.h"
#include "./SystemApplet.h"
#include "graphics/niche/Utils/FlashData.h"

using namespace NicheGraphics;

InkHUD::Events::Events()
{
    // Get convenient references
    inkhud = InkHUD::getInstance();
    settings = &inkhud->persistence->settings;
}

void InkHUD::Events::begin()
{
    // Register our callbacks for the various events

    deepSleepObserver.observe(&notifyDeepSleep);
    rebootObserver.observe(&notifyReboot);
    textMessageObserver.observe(textMessageModule);
#if !MESHTASTIC_EXCLUDE_ADMIN
    adminMessageObserver.observe((Observable<AdminModule_ObserverData *> *)adminModule);
#endif
#ifdef ARCH_ESP32
    lightSleepObserver.observe(&notifyLightSleep);
#endif
}

void InkHUD::Events::onButtonShort()
{
    // Audio feedback (via buzzer)
    // Short tone
    playChirp();
    // Cancel any beeping, buzzing, blinking
    // Some button handling suppressed if we are dismissing an external notification (see below)
    bool dismissedExt = dismissExternalNotification();

    // Check which system applet wants to handle the button press (if any)
    SystemApplet *consumer = nullptr;
    for (SystemApplet *sa : inkhud->systemApplets) {
        if (sa->handleInput) {
            consumer = sa;
            break;
        }
    }

    // If no system applet is handling input, default behavior instead is to cycle applets
    if (consumer)
        consumer->onButtonShortPress();
    else if (!dismissedExt) // Don't change applet if this button press silenced the external notification module
        inkhud->nextApplet();
}

void InkHUD::Events::onButtonLong()
{
    // Audio feedback (via buzzer)
    // Slightly longer than playChirp
    playBoop();

    // Check which system applet wants to handle the button press (if any)
    SystemApplet *consumer = nullptr;
    for (SystemApplet *sa : inkhud->systemApplets) {
        if (sa->handleInput) {
            consumer = sa;
            break;
        }
    }

    // If no system applet is handling input, default behavior instead is to open the menu
    if (consumer)
        consumer->onButtonLongPress();
    else
        inkhud->openMenu();
}

// Callback for deepSleepObserver
// Returns 0 to signal that we agree to sleep now
int InkHUD::Events::beforeDeepSleep(void *unused)
{
    // If a previous display update is in progress, wait for it to complete.
    inkhud->awaitUpdate();

    // Notify all applets that we're shutting down
    for (Applet *ua : inkhud->userApplets) {
        ua->onDeactivate();
        ua->onShutdown();
    }
    for (SystemApplet *sa : inkhud->systemApplets) {
        // Note: no onDeactivate. System applets are always active.
        sa->onShutdown();
    }

    // User has successful executed a safe shutdown
    // We don't need to nag at boot anymore
    settings->tips.safeShutdownSeen = true;

    inkhud->persistence->saveSettings();
    inkhud->persistence->saveLatestMessage();

    // LogoApplet::onShutdown attempted to heal the display by drawing a "shutting down" screen twice,
    // then prepared a final powered-off screen for us, which shows device shortname.
    // We're updating to show that one now.

    inkhud->forceUpdate(Drivers::EInk::UpdateTypes::FULL, false);
    delay(1000); // Cooldown, before potentially yanking display power

    // InkHUD shutdown complete
    // Firmware shutdown continues for several seconds more; flash write still pending
    playShutdownMelody();

    return 0; // We agree: deep sleep now
}

// Callback for rebootObserver
// Same as shutdown, without drawing the logoApplet
// Makes sure we don't lose message history / InkHUD config
int InkHUD::Events::beforeReboot(void *unused)
{

    // Notify all applets that we're "shutting down"
    // They don't need to know that it's really a reboot
    for (Applet *a : inkhud->userApplets) {
        a->onDeactivate();
        a->onShutdown();
    }
    for (SystemApplet *sa : inkhud->systemApplets) {
        // Note: no onDeactivate. System applets are always active.
        sa->onReboot();
    }

    // Save settings to flash, or erase if factory reset in progress
    if (!eraseOnReboot) {
        inkhud->persistence->saveSettings();
        inkhud->persistence->saveLatestMessage();
    } else {
        NicheGraphics::clearFlashData();
    }

    // Note: no forceUpdate call here
    // We don't have any final screen to draw, although LogoApplet::onReboot did already display a "rebooting" screen

    return 0; // No special status to report. Ignored anyway by this Observable
}

// Callback when a new text message is received
// Caches the most recently received message, for use by applets
// Rx does not trigger a save to flash, however the data *will* be saved alongside other during shutdown, etc.
// Note: this is different from devicestate.rx_text_message, which may contain an *outgoing* message
int InkHUD::Events::onReceiveTextMessage(const meshtastic_MeshPacket *packet)
{
    // Short circuit: don't store outgoing messages
    if (getFrom(packet) == nodeDB->getNodeNum())
        return 0;

    // Determine whether the message is broadcast or a DM
    // Store this info to prevent confusion after a reboot
    // Avoids need to compare timestamps, because of situation where "future" messages block newly received, if time not set
    inkhud->persistence->latestMessage.wasBroadcast = isBroadcast(packet->to);

    // Pick the appropriate variable to store the message in
    MessageStore::Message *storedMessage = inkhud->persistence->latestMessage.wasBroadcast
                                               ? &inkhud->persistence->latestMessage.broadcast
                                               : &inkhud->persistence->latestMessage.dm;

    // Store nodenum of the sender
    // Applets can use this to fetch user data from nodedb, if they want
    storedMessage->sender = packet->from;

    // Store the time (epoch seconds) when message received
    storedMessage->timestamp = getValidTime(RTCQuality::RTCQualityDevice, true); // Current RTC time

    // Store the channel
    // - (potentially) used to determine whether notification shows
    // - (potentially) used to determine which applet to focus
    storedMessage->channelIndex = packet->channel;

    // Store the text
    // Need to specify manually how many bytes, because source not null-terminated
    storedMessage->text =
        std::string(&packet->decoded.payload.bytes[0], &packet->decoded.payload.bytes[packet->decoded.payload.size]);

    return 0; // Tell caller to continue notifying other observers. (No reason to abort this event)
}

int InkHUD::Events::onAdminMessage(AdminModule_ObserverData *data)
{
    switch (data->request->which_payload_variant) {
    // Factory reset
    // Two possible messages. One preserves BLE bonds, other wipes. Both should clear InkHUD data.
    case meshtastic_AdminMessage_factory_reset_device_tag:
    case meshtastic_AdminMessage_factory_reset_config_tag:
        eraseOnReboot = true;
        *data->result = AdminMessageHandleResult::HANDLED;
        break;

    default:
        break;
    }

    return 0; // Tell caller to continue notifying other observers. (No reason to abort this event)
}

#ifdef ARCH_ESP32
// Callback for lightSleepObserver
// Make sure the display is not partway through an update when we begin light sleep
// This is because some displays require active input from us to terminate the update process, and protect the panel hardware
int InkHUD::Events::beforeLightSleep(void *unused)
{
    inkhud->awaitUpdate();
    return 0; // No special status to report. Ignored anyway by this Observable
}
#endif

// Silence all ongoing beeping, blinking, buzzing, coming from the external notification module
// Returns true if an external notification was active, and we dismissed it
// Button handling changes depending on our result
bool InkHUD::Events::dismissExternalNotification()
{
    // Abort if not using external notifications
    if (!moduleConfig.external_notification.enabled)
        return false;

    // Abort if nothing to dismiss
    if (!externalNotificationModule->nagging())
        return false;

    // Stop the beep buzz blink
    externalNotificationModule->stopNow();

    // Inform that we did indeed dismiss an external notification
    return true;
}

#endif