#include "DetectionSensorModule.h"
#include "Default.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "main.h"
#include <Throttle.h>
#ifdef ARCH_NRF52
#include "sleep.h"
#endif

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

    if (moduleConfig.detection_sensor.enabled == false)
        return disable();

#ifdef ARCH_NRF52
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR && config.power.is_power_saving) {
        nRFSenseSleep = true;
    }
#endif
    
    if (firstTime) {

#ifdef DETECTION_SENSOR_EN
        pinMode(DETECTION_SENSOR_EN, OUTPUT);
        digitalWrite(DETECTION_SENSOR_EN, HIGH);
#endif

        // This is the first time the OSThread library has called this function, so do some setup
        firstTime = false;

        if (moduleConfig.detection_sensor.monitor_pin > 0) {
            if (nRFSenseSleep) {
                nrf_gpio_pin_sense_t detectsense;
                switch (moduleConfig.detection_sensor.detection_trigger_type) {
                    case meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType_LOGIC_LOW:
                    case meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType_FALLING_EDGE:
                        detectsense = NRF_GPIO_PIN_SENSE_LOW;
                        break;
                    case meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType_LOGIC_HIGH:
                    case meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType_RISING_EDGE:
                        detectsense = NRF_GPIO_PIN_SENSE_HIGH;
                        break;
                    default:
                        LOG_ERROR("cant detect 'any edge', defaulting to falling edge");
                        detectsense = NRF_GPIO_PIN_SENSE_LOW;
                }
                nrf_gpio_cfg_input(moduleConfig.detection_sensor.monitor_pin,
                                moduleConfig.detection_sensor.use_pullup
                                    ? NRF_GPIO_PIN_PULLUP
                                    : NRF_GPIO_PIN_NOPULL);

                nrf_gpio_cfg_sense_set(
                    moduleConfig.detection_sensor.monitor_pin, detectsense);
                
                if (NRF_P0->LATCH || NRF_P1->LATCH) {
                    uint32_t gpioMask = NRF_P0->LATCH ? NRF_P0->LATCH : NRF_P1->LATCH;
                    uint32_t gpioPort = NRF_P0->LATCH ? 0 : 1;
                    NRF_P1->LATCH = 0xFFFFFFFF;
                    NRF_P0->LATCH = 0xFFFFFFFF;
                    LOG_INFO("Woke up by interrupt from GPIO %d on port P%d. Sending message.", __builtin_ctz(gpioMask), gpioPort);
                    sendDetectionMessage();
                } 

                LOG_INFO("Detection Sensor Module: init in interrupt mode");
                // go to sleep after ~90s, this allows remote configuration and in case a wake up by timer the sending of telemetry data (after ~62s)
                return 1000 * 90;    //90s
                
            } else {
                pinMode(moduleConfig.detection_sensor.monitor_pin,
                        moduleConfig.detection_sensor.use_pullup ? INPUT_PULLUP
                                                                : INPUT);
            }
        } else {
            LOG_WARN("Detection Sensor Module: Set to enabled but no monitor pin is set. Disable module");
            return disable();
        }
        LOG_INFO("Detection Sensor Module: init");

        return setStartDelay();
    }

    if (nRFSenseSleep) {
        uint32_t nightyNightMs = Default::getConfiguredOrMinimumValue(Default::getConfiguredOrDefaultMs(config.power.sds_secs), ONE_MINUTE_MS * 60);
        LOG_DEBUG("Sleeping for %ims", nightyNightMs);
        doDeepSleep(nightyNightMs, false, true);
    }

    // LOG_DEBUG("Detection Sensor Module: Current pin state: %i", digitalRead(moduleConfig.detection_sensor.monitor_pin));

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
    LOG_DEBUG("Detected event observed. Send message");
    char *message = new char[40];
    sprintf(message, "%s detected", moduleConfig.detection_sensor.name);
    meshtastic_MeshPacket *p = allocDataPacket();
    p->want_ack = false;
    p->decoded.payload.size = strlen(message);
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR)
        p->priority = meshtastic_MeshPacket_Priority_RELIABLE;
    
    memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);
    if (moduleConfig.detection_sensor.send_bell && p->decoded.payload.size < meshtastic_Constants_DATA_PAYLOAD_LEN) {
        p->decoded.payload.bytes[p->decoded.payload.size] = 7;        // Bell character
        p->decoded.payload.bytes[p->decoded.payload.size + 1] = '\0'; // Bell character
        p->decoded.payload.size++;
    }
    lastSentToMesh = millis();
    if (!channels.isDefaultChannel(0)) {
        LOG_INFO("Send message id=%d, dest=%x, msg=%.*s", p->id, p->to, p->decoded.payload.size, p->decoded.payload.bytes);
        service->sendToMesh(p);
    } else
        LOG_ERROR("Message not allow on Public channel");
    delete[] message;
}

void DetectionSensorModule::sendCurrentStateMessage(bool state)
{
    char *message = new char[40];
    sprintf(message, "%s state: %i", moduleConfig.detection_sensor.name, state);
    meshtastic_MeshPacket *p = allocDataPacket();
    p->want_ack = false;
    p->decoded.payload.size = strlen(message);
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR)
        p->priority = meshtastic_MeshPacket_Priority_RELIABLE;
    
    memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);
    lastSentToMesh = millis();
    if (!channels.isDefaultChannel(0)) {
        LOG_INFO("Send message id=%d, dest=%x, msg=%.*s", p->id, p->to, p->decoded.payload.size, p->decoded.payload.bytes);
        service->sendToMesh(p);
    } else
        LOG_ERROR("Message not allow on Public channel");
    delete[] message;
}

bool DetectionSensorModule::hasDetectionEvent()
{
    bool currentState = digitalRead(moduleConfig.detection_sensor.monitor_pin);
    // LOG_DEBUG("Detection Sensor Module: Current state: %i", currentState);
    return (moduleConfig.detection_sensor.detection_trigger_type & 1) ? currentState : !currentState;
}