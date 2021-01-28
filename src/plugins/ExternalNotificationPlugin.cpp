#include "ExternalNotificationPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include <Arduino.h>

#include <assert.h>

/*

    bool ext_notification_plugin_enabled = 126;
    uint32 ext_notification_plugin_output_ms = 127;
    uint32 ext_notification_plugin_output = 128;
    bool ext_notification_plugin_active = 129;
    bool ext_notification_plugin_alert_message = 130;
    bool ext_notification_plugin_alert_bell = 131;

*/

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

    if (1) {

        if (firstTime) {

            DEBUG_MSG("Initializing External Notification Plugin\n");

            externalNotificationPluginRadio = new ExternalNotificationPluginRadio();

            firstTime = 0;

            // Set the direction of a pin
            pinMode(13, OUTPUT);

            // if ext_notification_plugin_active
            if (1) { 
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
    // if ext_notification_plugin_active
    if (1) {
        digitalWrite(13, true);

    } else {
        digitalWrite(13, false);
    }
}

void ExternalNotificationPlugin::setExternalOff()
{
    // if ext_notification_plugin_active
    if (1) {
        digitalWrite(13, false);
    } else {
        digitalWrite(13, true);
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

    if (1) {

        auto &p = mp.decoded.data;

        if (mp.from != nodeDB.getNodeNum()) {

            /*
                TODO: If ext_notification_plugin_alert_bell is true and we see a bell character, trigger an external notification.
            */
            // TODO: Check p.payload.bytes to see if it contains a bell character. If it does, trigger an external notifcation.

            /*
                TODO: If ext_notification_plugin_alert_message is true, trigger an external notification.
            */
            // TODO: On received packet, blink the LED.
            externalNotificationPlugin->setExternalOn();
            delay(500);
            externalNotificationPlugin->setExternalOff();

        }

    } else {
        DEBUG_MSG("External Notification Plugin Disabled\n");
    }

#endif

    return true; // Let others look at this message also if they want
}
