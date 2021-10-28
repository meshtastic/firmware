#include "configuration.h"
#include "ExternalNotificationPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include <Arduino.h>

//#include <assert.h>

/*

    Documentation:
        https://github.com/meshtastic/Meshtastic-device/blob/master/docs/software/plugins/ExternalNotificationPlugin.md

    This plugin supports:
        https://github.com/meshtastic/Meshtastic-device/issues/654


    Quick reference:

        radioConfig.preferences.ext_notification_plugin_enabled
            0 = Disabled (Default)
            1 = Enabled

        radioConfig.preferences.ext_notification_plugin_active
            0 = Active Low (Default)
            1 = Active High

        radioConfig.preferences.ext_notification_plugin_alert_message
            0 = Disabled (Default)
            1 = Alert when a text message comes

        radioConfig.preferences.ext_notification_plugin_alert_bell
            0 = Disabled (Default)
            1 = Alert when the bell character is received

        radioConfig.preferences.ext_notification_plugin_output
            GPIO of the output. (Default = 13)

        radioConfig.preferences.ext_notification_plugin_output_ms
            Amount of time in ms for the alert. Default is 1000.

*/

// Default configurations
#define EXT_NOTIFICATION_PLUGIN_OUTPUT 13
#define EXT_NOTIFICATION_PLUGIN_OUTPUT_MS 1000

#define ASCII_BELL 0x07

bool externalCurrentState = 0;
uint32_t externalTurnedOn = 0;

int32_t ExternalNotificationPlugin::runOnce()
{
    /*
        Uncomment the preferences below if you want to use the plugin
        without having to configure it from the PythonAPI or WebUI.
    */

    // radioConfig.preferences.ext_notification_plugin_enabled = 1;
    // radioConfig.preferences.ext_notification_plugin_alert_message = 1;

    // radioConfig.preferences.ext_notification_plugin_active = 1;
    // radioConfig.preferences.ext_notification_plugin_alert_bell = 1;
    // radioConfig.preferences.ext_notification_plugin_output_ms = 1000;
    // radioConfig.preferences.ext_notification_plugin_output = 13;

    if (externalCurrentState) {

        // If the output is turned on, turn it back off after the given period of time.
        if (externalTurnedOn + (radioConfig.preferences.ext_notification_plugin_output_ms
                                    ? radioConfig.preferences.ext_notification_plugin_output_ms
                                    : EXT_NOTIFICATION_PLUGIN_OUTPUT_MS) <
            millis()) {
            DEBUG_MSG("Turning off external notification\n");
            setExternalOff();
        }
    }

    return (25);
}

void ExternalNotificationPlugin::setExternalOn()
{
    externalCurrentState = 1;
    externalTurnedOn = millis();

    digitalWrite((radioConfig.preferences.ext_notification_plugin_output ? radioConfig.preferences.ext_notification_plugin_output
                                                                         : EXT_NOTIFICATION_PLUGIN_OUTPUT),
                 (radioConfig.preferences.ext_notification_plugin_active ? true : false));
}

void ExternalNotificationPlugin::setExternalOff()
{
    externalCurrentState = 0;

    digitalWrite((radioConfig.preferences.ext_notification_plugin_output ? radioConfig.preferences.ext_notification_plugin_output
                                                                         : EXT_NOTIFICATION_PLUGIN_OUTPUT),
                 (radioConfig.preferences.ext_notification_plugin_active ? false : true));
}

// --------

ExternalNotificationPlugin::ExternalNotificationPlugin()
    : SinglePortPlugin("ExternalNotificationPlugin", PortNum_TEXT_MESSAGE_APP), concurrency::OSThread(
                                                                                         "ExternalNotificationPlugin")
{
    // restrict to the admin channel for rx
    boundChannel = Channels::gpioChannel;

#ifndef NO_ESP32

    /*
        Uncomment the preferences below if you want to use the plugin
        without having to configure it from the PythonAPI or WebUI.
    */

    // radioConfig.preferences.ext_notification_plugin_enabled = 1;
    // radioConfig.preferences.ext_notification_plugin_alert_message = 1;

    // radioConfig.preferences.ext_notification_plugin_active = 1;
    // radioConfig.preferences.ext_notification_plugin_alert_bell = 1;
    // radioConfig.preferences.ext_notification_plugin_output_ms = 1000;
    // radioConfig.preferences.ext_notification_plugin_output = 13;

    if (radioConfig.preferences.ext_notification_plugin_enabled) {

        DEBUG_MSG("Initializing External Notification Plugin\n");

        // Set the direction of a pin
        pinMode((radioConfig.preferences.ext_notification_plugin_output ? radioConfig.preferences.ext_notification_plugin_output
                                                                        : EXT_NOTIFICATION_PLUGIN_OUTPUT),
                OUTPUT);

        // Turn off the pin
        setExternalOff();
    } else {
        DEBUG_MSG("External Notification Plugin Disabled\n");
        enabled = false;
    }
#endif
}

ProcessMessage ExternalNotificationPlugin::handleReceived(const MeshPacket &mp)
{
#ifndef NO_ESP32

    if (radioConfig.preferences.ext_notification_plugin_enabled) {

        auto &p = mp.decoded;

        if (getFrom(&mp) != nodeDB.getNodeNum()) {

            // TODO: This may be a problem if messages are sent in unicide, but I'm not sure if it will.
            //   Need to know if and how this could be a problem.
            if (radioConfig.preferences.ext_notification_plugin_alert_bell) {
                DEBUG_MSG("externalNotificationPlugin - Notification Bell\n");
                for (int i = 0; i < p.payload.size; i++) {
                    if (p.payload.bytes[i] == ASCII_BELL) {
                        setExternalOn();
                    }
                }
            }

            if (radioConfig.preferences.ext_notification_plugin_alert_message) {
                DEBUG_MSG("externalNotificationPlugin - Notification Plugin\n");
                setExternalOn();
            }
        }

    } else {
        DEBUG_MSG("External Notification Plugin Disabled\n");
    }

#endif

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}
