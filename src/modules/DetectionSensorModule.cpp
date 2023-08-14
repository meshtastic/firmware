#include "DetectionSensorModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "main.h"

DetectionSensorModule *detectionSensorModule;

#define GPIO_POLLING_INTERVAL 50
#define DELAYED_INTERVAL 1000

int32_t DetectionSensorModule::runOnce()
{
    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
    */
    moduleConfig.detection_sensor.enabled = true;
    moduleConfig.detection_sensor.monitor_pin = 10; // WisBlock PIR IO6
    moduleConfig.detection_sensor.minimum_broadcast_secs = 60;
    moduleConfig.detection_sensor.detection_triggered_high = false;
    strcpy(moduleConfig.detection_sensor.name, "Motion");

    if (moduleConfig.detection_sensor.enabled == false)
        return disable();

    if (firstTime) {
        // This is the first time the OSThread library has called this function, so do some setup
        firstTime = false;
        if (moduleConfig.detection_sensor.monitor_pin > 0) {
            pinMode(moduleConfig.detection_sensor.monitor_pin, INPUT);
        }
        LOG_INFO("Detection Sensor Module: Initializing\n");

        return DELAYED_INTERVAL;
    }

    if ((millis() - lastSentToMesh) >= getConfiguredOrDefaultMs(moduleConfig.detection_sensor.minimum_broadcast_secs) &&
        hasStateChanged()) {
        sendDetectionMessage();
        return DELAYED_INTERVAL;
    }
    // Even if we haven't detected a change, broadcast to the mesh on our scheduled interval as a sort of heartbeat
    else if ((millis() - lastSentToMesh) >= getConfiguredOrDefaultMs(moduleConfig.detection_sensor.state_broadcast_secs)) {
        return DELAYED_INTERVAL;
    }
    // bool currentState = digitalRead(moduleConfig.detection_sensor.monitor_pin);
    // LOG_DEBUG("Detection Sensor Module: Current state: %i\n", currentState);
    return GPIO_POLLING_INTERVAL;
}

void DetectionSensorModule::sendDetectionMessage()
{
    LOG_DEBUG("Detected state change. Sending message\n");
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
}

void DetectionSensorModule::sendCurrentStateMessage()
{
    char *message = new char[40];
    sprintf(message, "%s state: %i", moduleConfig.detection_sensor.name, hasStateChanged());

    meshtastic_MeshPacket *p = allocDataPacket();
    p->want_ack = false;
    p->decoded.payload.size = strlen(message);
    memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);
    LOG_INFO("Sending message id=%d, dest=%x, msg=%.*s\n", p->id, p->to, p->decoded.payload.size, p->decoded.payload.bytes);
    lastSentToMesh = millis();
    service.sendToMesh(p);
}

bool DetectionSensorModule::hasStateChanged()
{
    bool currentState = digitalRead(moduleConfig.detection_sensor.monitor_pin);
    LOG_DEBUG("Detection Sensor Module: Current state: %i\n", currentState);
    return moduleConfig.detection_sensor.detection_triggered_high ? currentState : !currentState;
}