#include "DetectionSensorModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "main.h"

DetectionSensorModule *detectionSensorModule;

#define GPIO_POLLING_INTERVAL 100
#define DELAYED_INTERVAL 1000

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
    // moduleConfig.detection_sensor.detection_triggered_high = true;
    // strcpy(moduleConfig.detection_sensor.name, "Motion");

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
            LOG_WARN("Detection Sensor Module: Set to enabled but no monitor pin is set. Disabling module...\n");
            return disable();
        }
        LOG_INFO("Detection Sensor Module: Initializing\n");

        return DELAYED_INTERVAL;
    }

    // LOG_DEBUG("Detection Sensor Module: Current pin state: %i\n", digitalRead(moduleConfig.detection_sensor.monitor_pin));

    if ((millis() - lastSentToMesh) >= getConfiguredOrDefaultMs(moduleConfig.detection_sensor.minimum_broadcast_secs) &&
        hasDetectionEvent()) {
        sendDetectionMessage();
        return DELAYED_INTERVAL;
    }
    // Even if we haven't detected an event, broadcast our current state to the mesh on the scheduled interval as a sort
    // of heartbeat. We only do this if the minimum broadcast interval is greater than zero, otherwise we'll only broadcast state
    // change detections.
    else if (moduleConfig.detection_sensor.state_broadcast_secs > 0 &&
             (millis() - lastSentToMesh) >= getConfiguredOrDefaultMs(moduleConfig.detection_sensor.state_broadcast_secs)) {
        sendCurrentStateMessage();
        return DELAYED_INTERVAL;
    }
    return GPIO_POLLING_INTERVAL;
}

void DetectionSensorModule::sendDetectionMessage()
{
    LOG_DEBUG("Detected event observed. Sending message\n");
    char *message = new char[40];
    sprintf(message, "%s detected", moduleConfig.detection_sensor.name);
    meshtastic_MeshPacket *p = allocDataPacket();
    p->want_ack = false;
    p->decoded.payload.size = strlen(message);
    memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);
    if (moduleConfig.detection_sensor.send_bell && p->decoded.payload.size < meshtastic_Constants_DATA_PAYLOAD_LEN) {
        p->decoded.payload.bytes[p->decoded.payload.size] = 7;        // Bell character
        p->decoded.payload.bytes[p->decoded.payload.size + 1] = '\0'; // Bell character
        p->decoded.payload.size++;
    }
    LOG_INFO("Sending message id=%d, dest=%x, msg=%.*s\n", p->id, p->to, p->decoded.payload.size, p->decoded.payload.bytes);
    lastSentToMesh = millis();
    service.sendToMesh(p);
    delete[] message;
}

void DetectionSensorModule::sendCurrentStateMessage()
{
    char *message = new char[40];
    sprintf(message, "%s state: %i", moduleConfig.detection_sensor.name, hasDetectionEvent());

    meshtastic_MeshPacket *p = allocDataPacket();
    p->want_ack = false;
    p->decoded.payload.size = strlen(message);
    memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);
    LOG_INFO("Sending message id=%d, dest=%x, msg=%.*s\n", p->id, p->to, p->decoded.payload.size, p->decoded.payload.bytes);
    lastSentToMesh = millis();
    service.sendToMesh(p);
    delete[] message;
}

bool DetectionSensorModule::hasDetectionEvent()
{
    bool currentState = digitalRead(moduleConfig.detection_sensor.monitor_pin);
    // LOG_DEBUG("Detection Sensor Module: Current state: %i\n", currentState);
    return moduleConfig.detection_sensor.detection_triggered_high ? currentState : !currentState;
}