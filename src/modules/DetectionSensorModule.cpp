#include "DetectionSensorModule.h"
#include "Default.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "main.h"
#include <Channels.h>
#include <Throttle.h>

DetectionSensorModule *detectionSensorModule;

#define GPIO_POLLING_INTERVAL 100
#define DELAYED_INTERVAL 1000

typedef enum {
    DetectionSensorVerdictDetected,
    DetectionSensorVerdictSendState,
    DetectionSensorVerdictNoop,
} DetectionSensorTriggerVerdict;

typedef DetectionSensorTriggerVerdict (*DetectionSensorTriggerHandler)(bool prev, bool current);

static DetectionSensorTriggerVerdict detection_trigger_logic_level(bool prev, bool current)
{
    return current ? DetectionSensorVerdictDetected : DetectionSensorVerdictNoop;
}

static DetectionSensorTriggerVerdict detection_trigger_single_edge(bool prev, bool current)
{
    return (!prev && current) ? DetectionSensorVerdictDetected : DetectionSensorVerdictNoop;
}

static DetectionSensorTriggerVerdict detection_trigger_either_edge(bool prev, bool current)
{
    if (prev == current) {
        return DetectionSensorVerdictNoop;
    }
    return current ? DetectionSensorVerdictDetected : DetectionSensorVerdictSendState;
}

const static DetectionSensorTriggerHandler handlers[_meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType_MAX + 1] = {
    [meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType_LOGIC_LOW] = detection_trigger_logic_level,
    [meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType_LOGIC_HIGH] = detection_trigger_logic_level,
    [meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType_FALLING_EDGE] = detection_trigger_single_edge,
    [meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType_RISING_EDGE] = detection_trigger_single_edge,
    [meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType_EITHER_EDGE_ACTIVE_LOW] = detection_trigger_either_edge,
    [meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType_EITHER_EDGE_ACTIVE_HIGH] = detection_trigger_either_edge,
};

int32_t DetectionSensorModule::runOnce()
{
    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
    */
    // moduleConfig.detection_sensor.enabled = true;
    // moduleConfig.detection_sensor.monitor_pin = 10; // WisBlock PIR IO6
    // moduleConfig.detection_sensor.monitor_pin = 21; // WisBlock RAK12013 Radar IO6
    // moduleConfig.detection_sensor.minimum_broadcast_secs = 30;
    // moduleConfig.detection_sensor.state_broadcast_secs = 120;
    // moduleConfig.detection_sensor.detection_trigger_type =
    // meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType_LOGIC_HIGH;
    // strcpy(moduleConfig.detection_sensor.name, "Motion");
    // moduleConfig.detection_sensor.sendTochannel = true; // Set false, msg will be send to UserID else sending to Channel Index.
    // moduleConfig.detection_sensor.sendTo =  1; // Send to UserID or Channel Index

    if (moduleConfig.detection_sensor.enabled == false)
        return disable();

    if (firstTime) {

#ifdef DETECTION_SENSOR_EN
        pinMode(DETECTION_SENSOR_EN, OUTPUT);
        digitalWrite(DETECTION_SENSOR_EN, HIGH);
#endif

        // This is the first time the OSThread library has called this function, so do some setup
        firstTime = false;
        if (moduleConfig.detection_sensor.monitor_pin > 0) {
            pinMode(moduleConfig.detection_sensor.monitor_pin, moduleConfig.detection_sensor.use_pullup ? INPUT_PULLUP : INPUT);
        } else {
            LOG_WARN("DetectionSensor: no monitor pin set. Disabling module...");
            return disable();
        }
        LOG_INFO("DetectionSensor: Init");

        return DELAYED_INTERVAL;
    }

    // LOG_DEBUG("Detection Sensor: Pin state: %i", digitalRead(moduleConfig.detection_sensor.monitor_pin));
    if (!Throttle::isWithinTimespanMs(lastSentToMesh,
                                    Default::getConfiguredOrDefaultMs(moduleConfig.detection_sensor.minimum_broadcast_secs))) {
        bool isDetected = hasDetectionEvent();
        DetectionSensorTriggerVerdict verdict =
            handlers[moduleConfig.detection_sensor.detection_trigger_type](wasDetected, isDetected);
        wasDetected = isDetected;
        switch (verdict) {
        case DetectionSensorVerdictDetected:
            sendDetectionMessage();
            return DELAYED_INTERVAL;
        case DetectionSensorVerdictSendState:
            sendCurrentStateMessage(isDetected);
            return DELAYED_INTERVAL;
        case DetectionSensorVerdictNoop:
            break;
        }
    }
    // Even if we haven't detected an event, broadcast our current state to the mesh on the scheduled interval as a sort
    // of heartbeat. We only do this if the minimum broadcast interval is greater than zero, otherwise we'll only broadcast state
    // change detections.
    if (moduleConfig.detection_sensor.state_broadcast_secs > 0 &&
        !Throttle::isWithinTimespanMs(lastSentToMesh,
                                    Default::getConfiguredOrDefaultMs(moduleConfig.detection_sensor.state_broadcast_secs,
                                                                        default_telemetry_broadcast_interval_secs))) {
        sendCurrentStateMessage(hasDetectionEvent());
        return DELAYED_INTERVAL;
    }
    return GPIO_POLLING_INTERVAL;
}

void DetectionSensorModule::sendDetectionMessage()
{
    if (moduleConfig.detection_sensor.sendTo != 0x00) {
        uint32_t msgDestination = moduleConfig.detection_sensor.sendTo;
        LOG_DEBUG("Detected event. Sending message");
        char *message = new char[40];
        bool isValidrecipient = false;
        sprintf(message, "%s detected", moduleConfig.detection_sensor.name);
        meshtastic_MeshPacket *p = allocDataPacket();
        p->want_ack = false;
        if (moduleConfig.detection_sensor.sendTochannel) {
            if (!channels.isDefaultChannel(msgDestination)) {
                p->channel = msgDestination;
                isValidrecipient = true;
            } else
                LOG_INFO("%d is public/not found", moduleConfig.detection_sensor.sendTo);
        } else {
            p->to = msgDestination;
            isValidrecipient = true;
        }
        p->decoded.payload.size = strlen(message);
        memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);
        if (moduleConfig.detection_sensor.send_bell && p->decoded.payload.size < meshtastic_Constants_DATA_PAYLOAD_LEN) {
            p->decoded.payload.bytes[p->decoded.payload.size] = 7;        // Bell character
            p->decoded.payload.bytes[p->decoded.payload.size + 1] = '\0'; // Bell character
            p->decoded.payload.size++;
        }
        lastSentToMesh = millis();
        if (isValidrecipient) {
            LOG_INFO("Send message id=%d, dest=%x, msg=%.*s", p->id,
                    (moduleConfig.detection_sensor.sendTochannel ? p->channel : p->to),
                    p->decoded.payload.size,
                    p->decoded.payload.bytes);
            service->sendToMesh(p);
        }
        delete[] message;
        } else
            LOG_INFO("DetectionSensor: no recipient");
}

void DetectionSensorModule::sendCurrentStateMessage(bool state)
{
    if (moduleConfig.detection_sensor.sendTo != 0x00) {
        uint32_t msgDestination = moduleConfig.detection_sensor.sendTo;
        char *message = new char[40];
        bool isValidrecipient = false;
        sprintf(message, "%s state: %i", moduleConfig.detection_sensor.name, state);
        meshtastic_MeshPacket *p = allocDataPacket();
        p->want_ack = false;
        if (moduleConfig.detection_sensor.sendTochannel) {
            if (!channels.isDefaultChannel(msgDestination)) {
                p->channel = msgDestination;
                isValidrecipient = true;
            } else
                LOG_INFO("%s is public/not found", moduleConfig.detection_sensor.sendTo);
        } else {
            p->to = msgDestination;
            isValidrecipient = true;
        }
        p->decoded.payload.size = strlen(message);
        memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);
        lastSentToMesh = millis();
        if (isValidrecipient) {
            LOG_INFO("Send message id=%d, dest=%x, msg=%.*s", p->id,
                (moduleConfig.detection_sensor.sendTochannel ? p->channel : p->to),
                p->decoded.payload.size,
                p->decoded.payload.bytes);
            service->sendToMesh(p);
        }
        delete[] message;
        } else
            LOG_INFO("Detection Sensor: no recipient");
}

bool DetectionSensorModule::hasDetectionEvent()
{
    bool currentState = digitalRead(moduleConfig.detection_sensor.monitor_pin);
    // LOG_DEBUG("Detection Sensor: state: %i", currentState);
    return (moduleConfig.detection_sensor.detection_trigger_type & 1) ? currentState : !currentState;
}
