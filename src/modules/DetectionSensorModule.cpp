#include "DetectionSensorModule.h"
#include "Default.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "main.h"
#include <Throttle.h>
DetectionSensorModule *detectionSensorModule;

// Safety-net cadence only; the attached CHANGE interrupt handles real transitions immediately.
#define GPIO_POLLING_INTERVAL 1000
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

// The configured trigger type arrives as an unvalidated protobuf enum, so a value outside the
// generated range would index past the handler table. Fall back to the schema default instead.
static meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType configuredTriggerType()
{
    const uint32_t configured = (uint32_t)moduleConfig.detection_sensor.detection_trigger_type;
    if (configured > (uint32_t)_meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType_MAX)
        return _meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType_MIN;
    return (meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType)configured;
}

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

    if (moduleConfig.detection_sensor.enabled == false)
        return disable();

    if (firstTime) {

#ifdef DETECTION_SENSOR_EN
        pinMode(DETECTION_SENSOR_EN, OUTPUT);
        digitalWrite(DETECTION_SENSOR_EN, HIGH);
#endif

        // This is the first time the OSThread library has called this function, so do some setup
        firstTime = false;
        if (!configureMonitorPin()) {
            LOG_WARN("Detection Sensor Module: Set to enabled but no monitor pin is set. Disable module");
            return disable();
        }
        LOG_INFO("Detection Sensor Module: init");

        return setStartDelay();
    }

    // LOG_DEBUG("Detection Sensor Module: Current pin state: %i", digitalRead(moduleConfig.detection_sensor.monitor_pin));

    // Pick up a runtime change to monitor_pin/use_pullup instead of leaving the interrupt bound to
    // a stale pin until reboot.
    if (moduleConfig.detection_sensor.monitor_pin != configuredMonitorPin ||
        moduleConfig.detection_sensor.use_pullup != configuredUsePullup) {
        if (!configureMonitorPin()) {
            LOG_WARN("Detection Sensor Module: monitor pin cleared at runtime. Disable module");
            return disable();
        }
    }
    // Also evaluated directly from the monitor-pin interrupt. The whole decide-and-clear sequence
    // stays inside one interrupt-disabled section - not just the update - so a verdict the interrupt
    // latches can't be read as part of one decision and then clobbered before the flag it belongs to
    // is actually cleared; the (possibly slow) send itself happens after interrupts are restored.
    noInterrupts();
    updatePendingVerdict();
    bool sendDetected = false;
    bool sendState = false;
    bool stateValue = false;
    if ((pendingDetected || pendingState) &&
        !Throttle::isWithinTimespanMs(lastSentToMesh,
                                      Default::getConfiguredOrDefaultMs(moduleConfig.detection_sensor.minimum_broadcast_secs))) {
        // Send whichever verdict occurred first when both are outstanding, so the mesh sees them
        // in the order they actually happened.
        if (pendingDetected && (!pendingState || pendingDetectedFirst)) {
            pendingDetected = false;
            sendDetected = true;
        } else {
            pendingState = false;
            stateValue = pendingStateIsDetected;
            sendState = true;
        }
    }
    interrupts();
    if (sendDetected) {
        sendDetectionMessage();
        return DELAYED_INTERVAL;
    }
    if (sendState) {
        sendCurrentStateMessage(stateValue);
        return DELAYED_INTERVAL;
    }
    // Even if we haven't detected an event, broadcast our current state to the mesh on the scheduled interval as a sort
    // of heartbeat. We only do this if the minimum broadcast interval is greater than zero, otherwise we'll only broadcast state
    // change detections.
    // Skipped while a verdict is pending: sending one resets lastSentToMesh, which could postpone
    // the pending verdict indefinitely if state_broadcast_secs < minimum_broadcast_secs.
    if (!pendingDetected && !pendingState && moduleConfig.detection_sensor.state_broadcast_secs > 0 &&
        !Throttle::isWithinTimespanMs(lastSentToMesh,
                                      Default::getConfiguredOrDefaultMs(moduleConfig.detection_sensor.state_broadcast_secs,
                                                                        default_telemetry_broadcast_interval_secs))) {
        sendCurrentStateMessage(wasDetected);
        return DELAYED_INTERVAL;
    }
    return GPIO_POLLING_INTERVAL;
}

void DetectionSensorModule::sendDetectionMessage()
{
    LOG_DEBUG("Detected event observed. Send message");
    char message[40];
    snprintf(message, sizeof(message), "%s detected", moduleConfig.detection_sensor.name);
    meshtastic_MeshPacket *p = allocDataPacket();
    if (!p) {
        return;
    }
    p->want_ack = false;
    p->decoded.payload.size = strlen(message);
    memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);
    if (moduleConfig.detection_sensor.send_bell && p->decoded.payload.size + 1 < meshtastic_Constants_DATA_PAYLOAD_LEN) {
        p->decoded.payload.bytes[p->decoded.payload.size] = 7;        // Bell character
        p->decoded.payload.bytes[p->decoded.payload.size + 1] = '\0'; // Bell character
        p->decoded.payload.size++;
    }
    lastSentToMesh = millis();
    if (!channels.isDefaultChannel(0)) {
        LOG_INFO("Send message id=%d, dest=%x, msg=%.*s", p->id, p->to, p->decoded.payload.size, p->decoded.payload.bytes);
        service->sendToMesh(p);
    } else {
        LOG_ERROR("Message not allow on Public channel");
        packetPool.release(p);
    }
}

void DetectionSensorModule::sendCurrentStateMessage(bool state)
{
    char message[40];
    snprintf(message, sizeof(message), "%s state: %i", moduleConfig.detection_sensor.name, state);
    meshtastic_MeshPacket *p = allocDataPacket();
    if (!p) {
        return;
    }
    p->want_ack = false;
    p->decoded.payload.size = strlen(message);
    memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);
    lastSentToMesh = millis();
    if (!channels.isDefaultChannel(0)) {
        LOG_INFO("Send message id=%d, dest=%x, msg=%.*s", p->id, p->to, p->decoded.payload.size, p->decoded.payload.bytes);
        service->sendToMesh(p);
    } else {
        LOG_ERROR("Message not allow on Public channel");
        packetPool.release(p);
    }
}

bool DetectionSensorModule::hasDetectionEvent()
{
    // Read the pin actually behind pinMode()/attachInterrupt(), not moduleConfig directly: on a
    // runtime pin change, moduleConfig updates before the next poll gets a chance to rebind, so the
    // still-armed interrupt must keep sampling the pin it's actually attached to.
    bool currentState = digitalRead(configuredMonitorPin);
    // LOG_DEBUG("Detection Sensor Module: Current state: %i", currentState);
    return (configuredTriggerType() & 1) ? currentState : !currentState;
}

bool DetectionSensorModule::configureMonitorPin()
{
    // Disabled for the whole rebind: otherwise an old-pin interrupt that's already in flight when
    // this starts could latch a genuine verdict via updatePendingVerdict() right before the reset
    // below discards it.
    noInterrupts();
    if (configuredMonitorPin > 0)
        detachInterrupt(configuredMonitorPin);
    configuredMonitorPin = moduleConfig.detection_sensor.monitor_pin;
    configuredUsePullup = moduleConfig.detection_sensor.use_pullup;
    // A pin/pullup change invalidates any tracked state from the old pin.
    wasDetected = false;
    pendingDetected = false;
    pendingState = false;
    pendingDetectedFirst = false;
    if (configuredMonitorPin == 0) {
        interrupts();
        return false;
    }
    pinMode(configuredMonitorPin, configuredUsePullup ? INPUT_PULLUP : INPUT);
    // Evaluate the trigger right here in the ISR (safe: just a digitalRead and bool bookkeeping, no
    // allocation/logging/sending) so a transition is captured the instant it happens, then wake the
    // module to send it - instead of relying solely on the GPIO_POLLING_INTERVAL fallback below,
    // which could miss a transition that reverses before the thread gets scheduled.
    attachInterrupt(
        configuredMonitorPin,
        []() {
            detectionSensorModule->updatePendingVerdict();
            detectionSensorModule->setIntervalFromNow(0);
            runASAP = true;
        },
        CHANGE);
    interrupts();
    return true;
}

void DetectionSensorModule::updatePendingVerdict()
{
    bool isDetected = hasDetectionEvent();
    DetectionSensorTriggerVerdict verdict = handlers[configuredTriggerType()](wasDetected, isDetected);
    wasDetected = isDetected;
    switch (verdict) {
    case DetectionSensorVerdictDetected:
        if (!pendingDetected)
            pendingDetectedFirst = !pendingState;
        pendingDetected = true;
        break;
    case DetectionSensorVerdictSendState:
        if (!pendingState)
            pendingDetectedFirst = pendingDetected;
        pendingState = true;
        pendingStateIsDetected = isDetected;
        break;
    case DetectionSensorVerdictNoop:
        break;
    }
}
