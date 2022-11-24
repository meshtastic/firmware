#include "ExternalNotificationModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "buzz/buzz.h"
#include "configuration.h"
#include <Arduino.h>

#ifndef PIN_BUZZER
#define PIN_BUZZER false
#endif

//#include <assert.h>

/*

    Documentation:
        https://github.com/meshtastic/firmware/blob/master/docs/software/modules/ExternalNotificationModule.md

    This module supports:
        https://github.com/meshtastic/firmware/issues/654


    Quick reference:

        moduleConfig.external_notification.enabled
            0 = Disabled (Default)
            1 = Enabled

        moduleConfig.external_notification.active
            0 = Active Low (Default)
            1 = Active High

        moduleConfig.external_notification.alert_message
            0 = Disabled (Default)
            1 = Alert when a text message comes

        moduleConfig.external_notification.alert_bell
            0 = Disabled (Default)
            1 = Alert when the bell character is received

        moduleConfig.external_notification.output
            GPIO of the output. (Default = 13)

        moduleConfig.external_notification.output_ms
            Amount of time in ms for the alert. Default is 1000.

*/

// Default configurations
#ifdef EXT_NOTIFY_OUT
#define EXT_NOTIFICATION_MODULE_OUTPUT EXT_NOTIFY_OUT
#else
#define EXT_NOTIFICATION_MODULE_OUTPUT 0
#endif
#define EXT_NOTIFICATION_MODULE_OUTPUT_MS 1000

#define ASCII_BELL 0x07

bool externalCurrentState = 0;
uint32_t externalTurnedOn = 0;

int32_t ExternalNotificationModule::runOnce()
{
    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
    */

    // moduleConfig.external_notification.enabled = 1;
    // moduleConfig.external_notification.alert_message = 1;

    // moduleConfig.external_notification.active = 1;
    // moduleConfig.external_notification.alert_bell = 1;
    // moduleConfig.external_notification.output_ms = 1000;
    // moduleConfig.external_notification.output = 13;

    if (externalCurrentState && !moduleConfig.external_notification.use_pwm) {

        // If the output is turned on, turn it back off after the given period of time.
        if (externalTurnedOn + (moduleConfig.external_notification.output_ms
                                    ? moduleConfig.external_notification.output_ms
                                    : EXT_NOTIFICATION_MODULE_OUTPUT_MS) <
            millis()) {
            DEBUG_MSG("Turning off external notification\n");
            setExternalOff();
        }
    }
    if (moduleConfig.external_notification.use_pwm)
        return INT32_MAX; // we don't need this thread here...
    else
        return 25;
}

void ExternalNotificationModule::setExternalOn()
{
    externalCurrentState = 1;
    externalTurnedOn = millis();

    digitalWrite(output,
                 (moduleConfig.external_notification.active ? true : false));
}

void ExternalNotificationModule::setExternalOff()
{
    externalCurrentState = 0;

    digitalWrite(output,
                 (moduleConfig.external_notification.active ? false : true));
}

// --------

ExternalNotificationModule::ExternalNotificationModule()
    : SinglePortModule("ExternalNotificationModule", PortNum_TEXT_MESSAGE_APP), concurrency::OSThread(
                                                                                    "ExternalNotificationModule")
{
    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
    */

    // moduleConfig.external_notification.enabled = 1;
    // moduleConfig.external_notification.alert_message = 1;

    // moduleConfig.external_notification.active = 1;
    // moduleConfig.external_notification.alert_bell = 1;
    // moduleConfig.external_notification.output_ms = 1000;
    // moduleConfig.external_notification.output = 13;
    
    if (moduleConfig.external_notification.enabled) {

        DEBUG_MSG("Initializing External Notification Module\n");

        output = moduleConfig.external_notification.output
                        ? moduleConfig.external_notification.output
                        : EXT_NOTIFICATION_MODULE_OUTPUT;

        if (!moduleConfig.external_notification.use_pwm) {
            // Set the direction of a pin
            DEBUG_MSG("Using Pin %i in digital mode\n", output);
            pinMode(output, OUTPUT);
            // Turn off the pin
            setExternalOff();
        } else {
            config.device.buzzer_gpio = config.device.buzzer_gpio
                                ? config.device.buzzer_gpio
                                : PIN_BUZZER;
                                
            // in PWM Mode we force the buzzer pin if it is set
            DEBUG_MSG("Using Pin %i in PWM mode\n", config.device.buzzer_gpio);
        }
    } else {
        DEBUG_MSG("External Notification Module Disabled\n");
        enabled = false;
    }
}

ProcessMessage ExternalNotificationModule::handleReceived(const MeshPacket &mp)
{
    if (moduleConfig.external_notification.enabled) {

        if (getFrom(&mp) != nodeDB.getNodeNum()) {

            // TODO: This may be a problem if messages are sent in unicide, but I'm not sure if it will.
            //   Need to know if and how this could be a problem.
            if (moduleConfig.external_notification.alert_bell) {
                auto &p = mp.decoded;
                DEBUG_MSG("externalNotificationModule - Notification Bell\n");
                for (int i = 0; i < p.payload.size; i++) {
                    if (p.payload.bytes[i] == ASCII_BELL) {
                        if (!moduleConfig.external_notification.use_pwm) {
                            setExternalOn();
                        } else {
                            playBeep();
                        }
                    }
                }
            }

            if (moduleConfig.external_notification.alert_message) {
                DEBUG_MSG("externalNotificationModule - Notification Module\n");
                if (!moduleConfig.external_notification.use_pwm) {
                    setExternalOn();
                } else {
                    playBeep();
                }
            }
        }

    } else {
        DEBUG_MSG("External Notification Module Disabled\n");
    }

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}
