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

#define EXT_NOTIFICATION_PLUGIN_ENABLED 1
#define EXT_NOTIFICATION_PLUGIN_ACTIVE 1
#define EXT_NOTIFICATION_PLUGIN_ALERT_MESSAGE 1
#define EXT_NOTIFICATION_PLUGIN_ALERT_BELL 1
#define EXT_NOTIFICATION_PLUGIN_OUTPUT 13
#define EXT_NOTIFICATION_PLUGIN_OUTPUT_MS 100

#define ASCII_BELL 0x07

ExternalNotificationPlugin *externalNotificationPlugin;
ExternalNotificationPluginRadio *externalNotificationPluginRadio;

ExternalNotificationPlugin::ExternalNotificationPlugin() : concurrency::OSThread("ExternalNotificationPlugin") {}

int32_t ExternalNotificationPlugin::runOnce()
{
#ifndef NO_ESP32

    /*
        Uncomment the preferences below if you want to use the plugin
        without having to configure it from the PythonAPI or WebUI.
    */

    // radioConfig.preferences.externalnotificationplugin_enabled = 1;
    // radioConfig.preferences.externalnotificationplugin_mode = 1;

    if (EXT_NOTIFICATION_PLUGIN_ENABLED) {

        if (firstTime) {

            DEBUG_MSG("Initializing External Notification Plugin\n");

            externalNotificationPluginRadio = new ExternalNotificationPluginRadio();

            firstTime = 0;

            // Set the direction of a pin
            pinMode(EXT_NOTIFICATION_PLUGIN_OUTPUT, OUTPUT);

            // if ext_notification_plugin_active
            if (EXT_NOTIFICATION_PLUGIN_ACTIVE) {
                setExternalOff();
            } else {
                setExternalOn();
            }

        } else {
            /*

                1) If GPIO is turned on ...
                2) Check the timer. If the timer has elapsed more time than
                   our set limit, turn the GPIO off.

            */
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

    // if ext_notification_plugin_active
    if (EXT_NOTIFICATION_PLUGIN_ACTIVE) {
        digitalWrite(EXT_NOTIFICATION_PLUGIN_OUTPUT, true);

    } else {
        digitalWrite(EXT_NOTIFICATION_PLUGIN_OUTPUT, false);
    }
}

void ExternalNotificationPlugin::setExternalOff()
{
    externalCurrentState = 0;

    // if ext_notification_plugin_active
    if (EXT_NOTIFICATION_PLUGIN_ACTIVE) {
        digitalWrite(EXT_NOTIFICATION_PLUGIN_OUTPUT, false);
    } else {
        digitalWrite(EXT_NOTIFICATION_PLUGIN_OUTPUT, true);
    }
}

// --------

MeshPacket *ExternalNotificationPluginRadio::allocReply()
{

    auto reply = allocDataPacket(); // Allocate a packet for sending

    return reply;
}

bool ExternalNotificationPluginRadio::handleReceived(const MeshPacket &mp)
{
#ifndef NO_ESP32

    if (EXT_NOTIFICATION_PLUGIN_ENABLED) {

        auto &p = mp.decoded.data;

        if (mp.from != nodeDB.getNodeNum()) {

            if (EXT_NOTIFICATION_PLUGIN_ALERT_BELL) {
                for (int i = 0; i < p.payload.size; i++) {
                    if (p.payload.bytes[i] == ASCII_BELL) {
                        externalNotificationPlugin->setExternalOn();

                        // TODO: Make this non-blocking.
                        delay(EXT_NOTIFICATION_PLUGIN_OUTPUT_MS);
                        externalNotificationPlugin->setExternalOff();
                    }
                }
            }

            if (EXT_NOTIFICATION_PLUGIN_ALERT_MESSAGE) {
                externalNotificationPlugin->setExternalOn();

                // TODO: Make this non-blocking.
                delay(EXT_NOTIFICATION_PLUGIN_OUTPUT_MS);
                externalNotificationPlugin->setExternalOff();
            }
        }

    } else {
        DEBUG_MSG("External Notification Plugin Disabled\n");
    }

#endif

    return true; // Let others look at this message also if they want
}
