#include "ExternalNotificationPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include <Arduino.h>

#include <assert.h>

/*

This plugin supports:
  https://github.com/meshtastic/Meshtastic-device/issues/654


    bool ext_notification_plugin_enabled = 126;
    bool ext_notification_plugin_active = 129;
    bool ext_notification_plugin_alert_message = 130;
    bool ext_notification_plugin_alert_bell = 131;
    uint32 ext_notification_plugin_output = 128;


    uint32 ext_notification_plugin_output_ms = 127;

*/

#define EXT_NOTIFICATION_PLUGIN_ENABLED 0
#define EXT_NOTIFICATION_PLUGIN_ACTIVE 0
#define EXT_NOTIFICATION_PLUGIN_ALERT_MESSAGE 0
#define EXT_NOTIFICATION_PLUGIN_ALERT_BELL 0
#define EXT_NOTIFICATION_PLUGIN_OUTPUT 13
#define EXT_NOTIFICATION_PLUGIN_OUTPUT_MS 1000

#define ASCII_BELL 0x07

ExternalNotificationPlugin *externalNotificationPlugin;
ExternalNotificationPluginRadio *externalNotificationPluginRadio;

ExternalNotificationPlugin::ExternalNotificationPlugin() : concurrency::OSThread("ExternalNotificationPlugin") {}

bool externalCurrentState = 0;
uint32_t externalTurnedOn = 0;

int32_t ExternalNotificationPlugin::runOnce()
{
#ifndef NO_ESP32

    /*
        Uncomment the preferences below if you want to use the plugin
        without having to configure it from the PythonAPI or WebUI.

        EXT_NOTIFICATION_PLUGIN_ENABLED
            0 = Disabled (Default)
            1 = Enabled

        EXT_NOTIFICATION_PLUGIN_ACTIVE
            0 = Active Low (Default)
            1 = Active High

        EXT_NOTIFICATION_PLUGIN_ALERT_MESSAGE
            0 = Disabled (Default)
            1 = Alert when a text message comes

        EXT_NOTIFICATION_PLUGIN_ALERT_BELL
            0 = Disabled (Default)
            1 = Alert when the bell character is received

        EXT_NOTIFICATION_PLUGIN_OUTPUT
            GPIO of the output. (Default = 13)

        EXT_NOTIFICATION_PLUGIN_OUTPUT_MS
            Amount of time in ms for the alert. Default is 1000.


    */

    if (EXT_NOTIFICATION_PLUGIN_ENABLED) {

        if (firstTime) {

            DEBUG_MSG("Initializing External Notification Plugin\n");

            // Set the direction of a pin
            pinMode(EXT_NOTIFICATION_PLUGIN_OUTPUT, OUTPUT);

            // Turn off the pin
            setExternalOff();

            externalNotificationPluginRadio = new ExternalNotificationPluginRadio();

            firstTime = 0;

        } else {
            if (externalCurrentState) {

                // If the output is turned on, turn it back off after the given period of time.
                if (externalTurnedOn + EXT_NOTIFICATION_PLUGIN_OUTPUT_MS < millis()) {
                    DEBUG_MSG("Turning off external notification\n");
                    setExternalOff();
                }
            }
        }

        return (25);
    } else {
        DEBUG_MSG("External Notification Plugin Disabled\n");

        return (INT32_MAX);
    }

#endif
}

void ExternalNotificationPlugin::setExternalOn()
{
    externalCurrentState = 1;
    externalTurnedOn = millis();

    // if ext_notification_plugin_active
    digitalWrite(EXT_NOTIFICATION_PLUGIN_OUTPUT, (EXT_NOTIFICATION_PLUGIN_ACTIVE ? true : false));
}

void ExternalNotificationPlugin::setExternalOff()
{
    externalCurrentState = 0;

    // if ext_notification_plugin_active
    digitalWrite(EXT_NOTIFICATION_PLUGIN_OUTPUT, (EXT_NOTIFICATION_PLUGIN_ACTIVE ? false : true));
}

// --------

bool ExternalNotificationPluginRadio::handleReceived(const MeshPacket &mp)
{
#ifndef NO_ESP32

    if (EXT_NOTIFICATION_PLUGIN_ENABLED) {

        auto &p = mp.decoded.data;
        // DEBUG_MSG("Processing handleReceived\n");

        if (mp.from != nodeDB.getNodeNum()) {
            DEBUG_MSG("handleReceived from some other device\n");

            if (EXT_NOTIFICATION_PLUGIN_ALERT_BELL) {
                for (int i = 0; i < p.payload.size; i++) {
                    if (p.payload.bytes[i] == ASCII_BELL) {
                        externalNotificationPlugin->setExternalOn();
                    }
                }
            }

            if (EXT_NOTIFICATION_PLUGIN_ALERT_MESSAGE) {
                // DEBUG_MSG("Turning on alert\n");
                externalNotificationPlugin->setExternalOn();
            }
        }

    } else {
        DEBUG_MSG("External Notification Plugin Disabled\n");
    }

#endif

    return true; // Let others look at this message also if they want
}
