#include "DetectionSensorModule.h"
#include "Default.h"
#include "DetectionSensorDwell.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "main.h"
#include <Throttle.h>
DetectionSensorModule *detectionSensorModule;

#define GPIO_POLLING_INTERVAL 100
#define DELAYED_INTERVAL 1000

// Trigger type only selects pin polarity (odd enum => active-high).
static bool configuredActiveHigh()
{
    const uint32_t configured = (uint32_t)moduleConfig.detection_sensor.detection_trigger_type;
    if (configured > (uint32_t)_meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType_MAX)
        return false;
    return (configured & 1U) != 0;
}

int32_t DetectionSensorModule::runOnce()
{
    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
    */
    // moduleConfig.detection_sensor.enabled = true;
    // moduleConfig.detection_sensor.monitor_pin = 21; // WisBlock RAK12013 Radar IO6
    // moduleConfig.detection_sensor.minimum_broadcast_secs = 30;
    // moduleConfig.detection_sensor.minimum_detect_secs = 1; // ignore sub-1s glitches
    // moduleConfig.detection_sensor.burst_gap_secs = 3;      // coalesce radar retriggers
    // moduleConfig.detection_sensor.minimum_alert_secs = 8;  // persistence before alert
    // moduleConfig.detection_sensor.send_clear = true;       // optional cleared timing msg
    // strcpy(moduleConfig.detection_sensor.name, "Driveway");

    if (moduleConfig.detection_sensor.enabled == false)
        return disable();

    if (firstTime) {

#ifdef DETECTION_SENSOR_EN
        pinMode(DETECTION_SENSOR_EN, OUTPUT);
        digitalWrite(DETECTION_SENSOR_EN, HIGH);
#endif

        firstTime = false;
        if (moduleConfig.detection_sensor.monitor_pin > 0) {
            pinMode(moduleConfig.detection_sensor.monitor_pin, moduleConfig.detection_sensor.use_pullup ? INPUT_PULLUP : INPUT);
        } else {
            LOG_WARN("Detection Sensor Module: Set to enabled but no monitor pin is set. Disable module");
            return disable();
        }
        LOG_INFO("Detection Sensor Module: init");

        return setStartDelay();
    }

    const uint32_t nowMs = millis();
    const bool pinActive = pinIsActive();
    if (pinActive && !pinWasActive) {
        pinActiveStartedMs = nowMs;
    }
    pinWasActive = pinActive;

    const bool dwellConfirmed = detectionSensorUpdateDwell(pinActive, moduleConfig.detection_sensor.minimum_detect_secs, nowMs,
                                                           dwellArmed, dwellStartedMs);

    const uint32_t burstStartCandidate =
        detectionSensorEpisodeStartMs(moduleConfig.detection_sensor.minimum_detect_secs, dwellStartedMs, pinActiveStartedMs);

    const DetectionSensorBurstResult burstOut =
        detectionSensorUpdateBurst(pinActive, dwellConfirmed, nowMs, moduleConfig.detection_sensor.burst_gap_secs,
                                   moduleConfig.detection_sensor.minimum_alert_secs, burstStartCandidate, burst);

    const bool canSendAlert = !Throttle::isWithinTimespanMs(
        lastSentToMesh, Default::getConfiguredOrDefaultMs(moduleConfig.detection_sensor.minimum_broadcast_secs));

    if (alertDeliveryPending && canSendAlert) {
        if (sendDetectionMessage(pendingAlertBurstMs)) {
            alertSentToMeshThisBurst = true;
            alertDeliveryPending = false;
            return DELAYED_INTERVAL;
        }
    }

    // Alerts honor minimum_broadcast_secs (cooldown between trips). Optional clear (send_clear)
    // still sends for a burst we alerted on, so duration isn't lost to the same cooldown.
    if (burstOut.event == DetectionSensorBurstEventAlert) {
        if (canSendAlert) {
            if (sendDetectionMessage(burstOut.burstMs)) {
                alertSentToMeshThisBurst = true;
                return DELAYED_INTERVAL;
            }
            alertDeliveryPending = true;
            pendingAlertBurstMs = burstOut.burstMs;
            LOG_WARN("Detection alert delivery failed; retrying");
        } else {
            alertSentToMeshThisBurst = false;
            LOG_DEBUG("Detection alert suppressed (broadcast cooldown)");
        }
    }
    if (burstOut.event == DetectionSensorBurstEventCleared) {
        alertDeliveryPending = false;
        if (alertSentToMeshThisBurst && moduleConfig.detection_sensor.send_clear) {
            sendClearedMessage(burstOut.activeMs, burstOut.burstMs);
            alertSentToMeshThisBurst = false;
            return DELAYED_INTERVAL;
        }
        alertSentToMeshThisBurst = false;
    }

    if (moduleConfig.detection_sensor.state_broadcast_secs > 0 &&
        !Throttle::isWithinTimespanMs(lastSentToMesh,
                                      Default::getConfiguredOrDefaultMs(moduleConfig.detection_sensor.state_broadcast_secs,
                                                                        default_telemetry_broadcast_interval_secs))) {
        sendCurrentStateMessage(pinIsActive());
        return DELAYED_INTERVAL;
    }
    return GPIO_POLLING_INTERVAL;
}

bool DetectionSensorModule::sendDetectionMessage(uint32_t burstMs)
{
    LOG_DEBUG("Detected event observed. Send message");
    char message[64];
    if (moduleConfig.detection_sensor.minimum_alert_secs > 0)
        snprintf(message, sizeof(message), "%s detected burst_ms=%u", moduleConfig.detection_sensor.name, (unsigned)burstMs);
    else
        snprintf(message, sizeof(message), "%s detected", moduleConfig.detection_sensor.name);
    meshtastic_MeshPacket *p = allocDataPacket();
    if (!p) {
        return false;
    }
    p->want_ack = false;
    p->decoded.payload.size = strlen(message);
    memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);
    if (moduleConfig.detection_sensor.send_bell && p->decoded.payload.size + 1 < meshtastic_Constants_DATA_PAYLOAD_LEN) {
        p->decoded.payload.bytes[p->decoded.payload.size] = 7;
        p->decoded.payload.bytes[p->decoded.payload.size + 1] = '\0';
        p->decoded.payload.size++;
    }
    if (!channels.isDefaultChannel(0)) {
        lastSentToMesh = millis();
        LOG_INFO("Send message id=%d, dest=%x, msg=%.*s", p->id, p->to, p->decoded.payload.size, p->decoded.payload.bytes);
        service->sendToMesh(p);
        return true;
    } else {
        LOG_ERROR("Message not allow on Public channel");
        service->releaseToPool(p);
        return false;
    }
}

void DetectionSensorModule::sendClearedMessage(uint32_t activeMs, uint32_t burstMs)
{
    char message[72];
    snprintf(message, sizeof(message), "%s cleared active_ms=%u burst_ms=%u", moduleConfig.detection_sensor.name,
             (unsigned)activeMs, (unsigned)burstMs);
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
        service->releaseToPool(p);
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
        service->releaseToPool(p);
    }
}

bool DetectionSensorModule::pinIsActive()
{
    bool currentState = digitalRead(moduleConfig.detection_sensor.monitor_pin);
    return configuredActiveHigh() ? currentState : !currentState;
}
