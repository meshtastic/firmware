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

/*
    Documentation:
        https://meshtastic.org/docs/settings/moduleconfig/external-notification
*/

// Default configurations
#ifdef EXT_NOTIFY_OUT
#define EXT_NOTIFICATION_MODULE_OUTPUT EXT_NOTIFY_OUT
#else
#define EXT_NOTIFICATION_MODULE_OUTPUT 0
#endif
#define EXT_NOTIFICATION_MODULE_OUTPUT_MS 1000

#define ASCII_BELL 0x07

ExternalNotificationModule *externalNotificationModule;

bool externalCurrentState[3] = {};

uint32_t externalTurnedOn[3] = {};

int32_t ExternalNotificationModule::runOnce()
{
    if (!moduleConfig.external_notification.enabled) {
        return INT32_MAX; // we don't need this thread here...
    } else {
#ifndef ARCH_PORTDUINO
        if ((nagCycleCutoff < millis()) && !rtttl::isPlaying()) {
#else
        if (nagCycleCutoff < millis()) {
#endif
            nagCycleCutoff = UINT32_MAX;
            DEBUG_MSG("Turning off external notification: ");
            for (int i = 0; i < 2; i++) {
                if (getExternal(i)) {
                    setExternalOff(i);
                    externalTurnedOn[i] = 0;
                    DEBUG_MSG("%d ", i);
                }
            }
            DEBUG_MSG("\n");
            return INT32_MAX; // save cycles till we're needed again
        }

        // If the output is turned on, turn it back off after the given period of time.
        if (nagCycleCutoff != UINT32_MAX) {
            if (externalTurnedOn[0] + (moduleConfig.external_notification.output_ms
                                    ? moduleConfig.external_notification.output_ms
                                    : EXT_NOTIFICATION_MODULE_OUTPUT_MS) < millis()) {
                getExternal(0) ? setExternalOff(0) : setExternalOn(0);
            }
            if (externalTurnedOn[1] + (moduleConfig.external_notification.output_ms
                                    ? moduleConfig.external_notification.output_ms
                                    : EXT_NOTIFICATION_MODULE_OUTPUT_MS) < millis()) {
                getExternal(1) ? setExternalOff(1) : setExternalOn(1);
            }
            if (externalTurnedOn[2] + (moduleConfig.external_notification.output_ms
                                    ? moduleConfig.external_notification.output_ms
                                    : EXT_NOTIFICATION_MODULE_OUTPUT_MS) < millis()) {
                getExternal(2) ? setExternalOff(2) : setExternalOn(2);
            }
        }

        // now let the PWM buzzer play
#ifndef ARCH_PORTDUINO
        if (moduleConfig.external_notification.use_pwm) {
            if (rtttl::isPlaying()) {
                rtttl::play();
            } else if (nagCycleCutoff >= millis()) {
                // start the song again if we have time left
                rtttl::begin(config.device.buzzer_gpio, pwmRingtone);
            }
        }
#endif        
        return 25;
    }
}

void ExternalNotificationModule::setExternalOn(uint8_t index)
{
    externalCurrentState[index] = 1;
    externalTurnedOn[index] = millis();

    switch(index) {
        case 1:
            if(moduleConfig.external_notification.output_vibra)
                digitalWrite(moduleConfig.external_notification.output_vibra, true);
            break;
        case 2:
            if(moduleConfig.external_notification.output_buzzer)
                digitalWrite(moduleConfig.external_notification.output_buzzer, true);
            break;
        default:
            digitalWrite(output, (moduleConfig.external_notification.active ? true : false));
            break;
    }
}

void ExternalNotificationModule::setExternalOff(uint8_t index)
{
    externalCurrentState[index] = 0;
    externalTurnedOn[index] = millis();

    switch(index) {
        case 1:
            if(moduleConfig.external_notification.output_vibra)
                digitalWrite(moduleConfig.external_notification.output_vibra, false);
            break;
        case 2:
            if(moduleConfig.external_notification.output_buzzer)
                digitalWrite(moduleConfig.external_notification.output_buzzer, false);
            break;
        default:
            digitalWrite(output, (moduleConfig.external_notification.active ? false : true));
            break;
    }
}

bool ExternalNotificationModule::getExternal(uint8_t index)
{
    return externalCurrentState[index];
}

void ExternalNotificationModule::stopNow() {
#ifndef ARCH_PORTDUINO    
    rtttl::stop();
#endif    
    nagCycleCutoff = 1; // small value
    setIntervalFromNow(0);
}

ExternalNotificationModule::ExternalNotificationModule()
    : SinglePortModule("ExternalNotificationModule", PortNum_TEXT_MESSAGE_APP), concurrency::OSThread(
                                                                                    "ExternalNotificationModule")
{
    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
    */

    // moduleConfig.external_notification.enabled = true;
    // moduleConfig.external_notification.alert_message = true;
    // moduleConfig.external_notification.alert_message_buzzer = true;
    // moduleConfig.external_notification.alert_message_vibra = true;

    // moduleConfig.external_notification.active = true;
    // moduleConfig.external_notification.alert_bell = 1;
    // moduleConfig.external_notification.output_ms = 1000;
    // moduleConfig.external_notification.output = 4; // RAK4631 IO4
    // moduleConfig.external_notification.output_buzzer = 10; // RAK4631 IO6
    // moduleConfig.external_notification.output_vibra = 28; // RAK4631 IO7
    // moduleConfig.external_notification.nag_timeout = 300;
    
    if (moduleConfig.external_notification.enabled) {

        DEBUG_MSG("Initializing External Notification Module\n");

        output = moduleConfig.external_notification.output
                        ? moduleConfig.external_notification.output
                        : EXT_NOTIFICATION_MODULE_OUTPUT;

        // Set the direction of a pin
        DEBUG_MSG("Using Pin %i in digital mode\n", output);
        pinMode(output, OUTPUT);
        setExternalOff(0);
        externalTurnedOn[0] = 0;
        if(moduleConfig.external_notification.output_vibra) {
            DEBUG_MSG("Using Pin %i for vibra motor\n", moduleConfig.external_notification.output_vibra);
            pinMode(moduleConfig.external_notification.output_vibra, OUTPUT);
            setExternalOff(1);
            externalTurnedOn[1] = 0;
        }
        if(moduleConfig.external_notification.output_buzzer) {
            if (!moduleConfig.external_notification.use_pwm) {
                DEBUG_MSG("Using Pin %i for buzzer\n", moduleConfig.external_notification.output_buzzer);
                pinMode(moduleConfig.external_notification.output_buzzer, OUTPUT);
                setExternalOff(2);
                externalTurnedOn[2] = 0;
            } else {
                config.device.buzzer_gpio = config.device.buzzer_gpio
                    ? config.device.buzzer_gpio
                    : PIN_BUZZER;
                // in PWM Mode we force the buzzer pin if it is set
                DEBUG_MSG("Using Pin %i in PWM mode\n", config.device.buzzer_gpio);
            }
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

            // Check if the message contains a bell character. Don't do this loop for every pin, just once.
            auto &p = mp.decoded;
            bool containsBell = false;
            for (int i = 0; i < p.payload.size; i++) {
                if (p.payload.bytes[i] == ASCII_BELL) {
                    containsBell = true;
                }
            }

            if (moduleConfig.external_notification.alert_bell) {
                if (containsBell) {
                    DEBUG_MSG("externalNotificationModule - Notification Bell\n");
                    setExternalOn(0);
                    if (moduleConfig.external_notification.nag_timeout) {
                        nagCycleCutoff = millis() + moduleConfig.external_notification.nag_timeout * 1000;
                    } else {
                        nagCycleCutoff = millis() + moduleConfig.external_notification.output_ms;
                    }
                }
            }

            if (moduleConfig.external_notification.alert_bell_vibra) {
                if (containsBell) {
                    DEBUG_MSG("externalNotificationModule - Notification Bell (Vibra)\n");
                    setExternalOn(1);
                    if (moduleConfig.external_notification.nag_timeout) {
                        nagCycleCutoff = millis() + moduleConfig.external_notification.nag_timeout * 1000;
                    } else {
                        nagCycleCutoff = millis() + moduleConfig.external_notification.output_ms;
                    }
                }
            }

            if (moduleConfig.external_notification.alert_bell_buzzer) {
                if (containsBell) {
                    DEBUG_MSG("externalNotificationModule - Notification Bell (Buzzer)\n");
                    if (!moduleConfig.external_notification.use_pwm) {
                        setExternalOn(2);
                    } else {
#ifndef ARCH_PORTDUINO
                        rtttl::begin(config.device.buzzer_gpio, pwmRingtone);
#endif
                    }
                    if (moduleConfig.external_notification.nag_timeout) {
                        nagCycleCutoff = millis() + moduleConfig.external_notification.nag_timeout * 1000;
                    } else {
                        nagCycleCutoff = millis() + moduleConfig.external_notification.output_ms;
                    }
                }
            }

            if (moduleConfig.external_notification.alert_message) {
                DEBUG_MSG("externalNotificationModule - Notification Module\n");
                setExternalOn(0);
                if (moduleConfig.external_notification.nag_timeout) {
                    nagCycleCutoff = millis() + moduleConfig.external_notification.nag_timeout * 1000;
                } else {
                    nagCycleCutoff = millis() + moduleConfig.external_notification.output_ms;
                }
            }

            if (!moduleConfig.external_notification.use_pwm) {
                if (moduleConfig.external_notification.alert_message_vibra) {
                    DEBUG_MSG("externalNotificationModule - Notification Module (Vibra)\n");
                    setExternalOn(1);
                    if (moduleConfig.external_notification.nag_timeout) {
                        nagCycleCutoff = millis() + moduleConfig.external_notification.nag_timeout * 1000;
                    } else {
                        nagCycleCutoff = millis() + moduleConfig.external_notification.output_ms;
                    }
                }

                if (moduleConfig.external_notification.alert_message_buzzer) {
                    DEBUG_MSG("externalNotificationModule - Notification Module (Buzzer)\n");
                    if (!moduleConfig.external_notification.use_pwm) {
                        setExternalOn(2);
                    } else {
#ifndef ARCH_PORTDUINO
                        rtttl::begin(config.device.buzzer_gpio, pwmRingtone);
#endif
                    }
                    if (moduleConfig.external_notification.nag_timeout) {
                        nagCycleCutoff = millis() + moduleConfig.external_notification.nag_timeout * 1000;
                    } else {
                        nagCycleCutoff = millis() + moduleConfig.external_notification.output_ms;
                    }
                }
            }
            setIntervalFromNow(0); // run once so we know if we should do something
        }

    } else {
        DEBUG_MSG("External Notification Module Disabled\n");
    }

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}
