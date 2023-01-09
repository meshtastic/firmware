#include "ExternalNotificationModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "buzz/buzz.h"
#include "configuration.h"
#include "mesh/generated/rtttl.pb.h"
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

RTTTLConfig rtttlConfig;

ExternalNotificationModule *externalNotificationModule;

bool externalCurrentState[3] = {};

uint32_t externalTurnedOn[3] = {};

static const char *rtttlConfigFile = "/prefs/ringtone.proto";

int32_t ExternalNotificationModule::runOnce()
{
    if (!moduleConfig.external_notification.enabled) {
        return INT32_MAX; // we don't need this thread here...
    } else {
        if ((nagCycleCutoff < millis()) && !rtttl::isPlaying()) {
            // let the song finish if we reach timeout
            nagCycleCutoff = UINT32_MAX;
            LOG_INFO("Turning off external notification: ");
            for (int i = 0; i < 2; i++) {
                setExternalOff(i);
                externalTurnedOn[i] = 0;
                LOG_INFO("%d ", i);
            }
            LOG_INFO("\n");
            isNagging = false;
            return INT32_MAX; // save cycles till we're needed again
        }

        // If the output is turned on, turn it back off after the given period of time.
        if (isNagging) {
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
        if (moduleConfig.external_notification.use_pwm) {
            if (rtttl::isPlaying()) {
                rtttl::play();
            } else if (isNagging && (nagCycleCutoff >= millis())) {
                // start the song again if we have time left
                rtttl::begin(config.device.buzzer_gpio, rtttlConfig.ringtone);
            }
        }
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
    rtttl::stop();
    nagCycleCutoff = 1; // small value
    isNagging = false;
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
        if (!nodeDB.loadProto(rtttlConfigFile, RTTTLConfig_size, sizeof(RTTTLConfig), &RTTTLConfig_msg, &rtttlConfig)) {
            memset(rtttlConfig.ringtone, 0, sizeof(rtttlConfig.ringtone));
            strncpy(rtttlConfig.ringtone, "a:d=8,o=5,b=125:4d#6,a#,2d#6,16p,g#,4a#,4d#.,p,16g,16a#,d#6,a#,f6,2d#6,16p,c#.6,16c6,16a#,g#.,2a#", sizeof(rtttlConfig.ringtone));
        }

        LOG_INFO("Initializing External Notification Module\n");

        output = moduleConfig.external_notification.output
                        ? moduleConfig.external_notification.output
                        : EXT_NOTIFICATION_MODULE_OUTPUT;

        // Set the direction of a pin
        LOG_INFO("Using Pin %i in digital mode\n", output);
        pinMode(output, OUTPUT);
        setExternalOff(0);
        externalTurnedOn[0] = 0;
        if(moduleConfig.external_notification.output_vibra) {
            LOG_INFO("Using Pin %i for vibra motor\n", moduleConfig.external_notification.output_vibra);
            pinMode(moduleConfig.external_notification.output_vibra, OUTPUT);
            setExternalOff(1);
            externalTurnedOn[1] = 0;
        }
        if(moduleConfig.external_notification.output_buzzer) {
            if (!moduleConfig.external_notification.use_pwm) {
                LOG_INFO("Using Pin %i for buzzer\n", moduleConfig.external_notification.output_buzzer);
                pinMode(moduleConfig.external_notification.output_buzzer, OUTPUT);
                setExternalOff(2);
                externalTurnedOn[2] = 0;
            } else {
                config.device.buzzer_gpio = config.device.buzzer_gpio
                    ? config.device.buzzer_gpio
                    : PIN_BUZZER;
                // in PWM Mode we force the buzzer pin if it is set
                LOG_INFO("Using Pin %i in PWM mode\n", config.device.buzzer_gpio);
            }
        }
    } else {
        LOG_INFO("External Notification Module Disabled\n");
        disable();
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
                    LOG_INFO("externalNotificationModule - Notification Bell\n");
                    isNagging = true;
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
                    LOG_INFO("externalNotificationModule - Notification Bell (Vibra)\n");
                    isNagging = true;
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
                    LOG_INFO("externalNotificationModule - Notification Bell (Buzzer)\n");
                    isNagging = true;
                    if (!moduleConfig.external_notification.use_pwm) {
                        setExternalOn(2);
                    } else {
                        rtttl::begin(config.device.buzzer_gpio, rtttlConfig.ringtone);
                    }
                    if (moduleConfig.external_notification.nag_timeout) {
                        nagCycleCutoff = millis() + moduleConfig.external_notification.nag_timeout * 1000;
                    } else {
                        nagCycleCutoff = millis() + moduleConfig.external_notification.output_ms;
                    }
                }
            }

            if (moduleConfig.external_notification.alert_message) {
                LOG_INFO("externalNotificationModule - Notification Module\n");
                isNagging = true;
                setExternalOn(0);
                if (moduleConfig.external_notification.nag_timeout) {
                    nagCycleCutoff = millis() + moduleConfig.external_notification.nag_timeout * 1000;
                } else {
                    nagCycleCutoff = millis() + moduleConfig.external_notification.output_ms;
                }
            }

            
            if (moduleConfig.external_notification.alert_message_vibra) {
                LOG_INFO("externalNotificationModule - Notification Module (Vibra)\n");
                isNagging = true;
                setExternalOn(1);
                if (moduleConfig.external_notification.nag_timeout) {
                    nagCycleCutoff = millis() + moduleConfig.external_notification.nag_timeout * 1000;
                } else {
                    nagCycleCutoff = millis() + moduleConfig.external_notification.output_ms;
                }
            }

            if (moduleConfig.external_notification.alert_message_buzzer) {
                LOG_INFO("externalNotificationModule - Notification Module (Buzzer)\n");
                isNagging = true;
                if (!moduleConfig.external_notification.use_pwm) {
                    setExternalOn(2);
                } else {
                    rtttl::begin(config.device.buzzer_gpio, rtttlConfig.ringtone);
                }
                if (moduleConfig.external_notification.nag_timeout) {
                    nagCycleCutoff = millis() + moduleConfig.external_notification.nag_timeout * 1000;
                } else {
                    nagCycleCutoff = millis() + moduleConfig.external_notification.output_ms;
                }
            }
            
            setIntervalFromNow(0); // run once so we know if we should do something
        }

    } else {
        LOG_INFO("External Notification Module Disabled\n");
    }

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

/**
 * @brief An admin message arrived to AdminModule. We are asked whether we want to handle that.
 *
 * @param mp The mesh packet arrived.
 * @param request The AdminMessage request extracted from the packet.
 * @param response The prepared response
 * @return AdminMessageHandleResult HANDLED if message was handled
 *   HANDLED_WITH_RESULT if a result is also prepared.
 */
AdminMessageHandleResult ExternalNotificationModule::handleAdminMessageForModule(const MeshPacket &mp, AdminMessage *request, AdminMessage *response)
{
    AdminMessageHandleResult result;

    switch (request->which_payload_variant) {
    case AdminMessage_get_ringtone_request_tag:
        LOG_INFO("Client is getting ringtone\n");
        this->handleGetRingtone(mp, response);
        result = AdminMessageHandleResult::HANDLED_WITH_RESPONSE;
        break;

    case AdminMessage_set_ringtone_message_tag:
        LOG_INFO("Client is setting ringtone\n");
        this->handleSetRingtone(request->set_canned_message_module_messages);
        result = AdminMessageHandleResult::HANDLED;
        break;

    default:
        result = AdminMessageHandleResult::NOT_HANDLED;
    }

    return result;
}

void ExternalNotificationModule::handleGetRingtone(const MeshPacket &req, AdminMessage *response)
{
    LOG_INFO("*** handleGetRingtone\n");
    if(req.decoded.want_response) {
        response->which_payload_variant = AdminMessage_get_ringtone_response_tag;
        strcpy(response->get_ringtone_response, rtttlConfig.ringtone);
    } // Don't send anything if not instructed to. Better than asserting.
}


void ExternalNotificationModule::handleSetRingtone(const char *from_msg)
{
    int changed = 0;

    if (*from_msg) {
        changed |= strcmp(rtttlConfig.ringtone, from_msg);
        strcpy(rtttlConfig.ringtone, from_msg);
        LOG_INFO("*** from_msg.text:%s\n", from_msg);
    }

    if (changed) {
        nodeDB.saveProto(rtttlConfigFile, RTTTLConfig_size, &RTTTLConfig_msg, &rtttlConfig);
    }
}