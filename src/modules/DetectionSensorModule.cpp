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
// Reserved values for GPREGRET are: 0xF5 (NRF52_MAGIC_LFS_IS_CORRUPT)
// and for the bootloader 0xB1, 0xA8, 0x4E, 0x57, 0x6D, lets choose free ones,
// although the bootloader shouldn't be called in the following soft reset.
constexpr uint32_t BOOT_FROM_COLD = 0x00;
constexpr uint32_t BOOT_FROM_TIMEOUT = 0xC0;
constexpr uint32_t BOOT_FROM_GPIOEVENT = 0xFE;
#elif defined(ESP32_WITH_EXT0)
#include "esp32/clk.h"
#include "sleep.h"
#include "soc/rtc.h"
#include <driver/rtc_io.h>
RTC_DATA_ATTR static uint32_t time_acc_s = 0;
RTC_DATA_ATTR static uint32_t sleepTime_s;
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

    if (firstTime) {
#ifdef DETECTION_SENSOR_EN
        pinMode(DETECTION_SENSOR_EN, OUTPUT);
        digitalWrite(DETECTION_SENSOR_EN, HIGH);
#endif
        // This is the first time the OSThread library has called this function, so do some setup
        firstTime = false;

        if (moduleConfig.detection_sensor.monitor_pin > 0) {
#ifdef ARCH_NRF52
            if (config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR && config.power.is_power_saving) {
                nrf_gpio_pin_sense_t toSense =
                    (moduleConfig.detection_sensor.detection_trigger_type & 1) ? NRF_GPIO_PIN_SENSE_HIGH : NRF_GPIO_PIN_SENSE_LOW;
                nrf_gpio_cfg_input(moduleConfig.detection_sensor.monitor_pin,
                                   moduleConfig.detection_sensor.use_pullup ? NRF_GPIO_PIN_PULLUP : NRF_GPIO_PIN_NOPULL);
                nrf_gpio_cfg_sense_set(moduleConfig.detection_sensor.monitor_pin, toSense);

                uint32_t regret = BOOT_FROM_COLD;
                if (sd_power_gpregret_get(0, &regret) != NRF_SUCCESS) {
                    // necessary if softdevice is not enabled yet
                    regret = NRF_POWER->GPREGRET;
                }
                if (NRF_P0->LATCH || NRF_P1->LATCH) {
                    LOG_INFO("Woke up from eternal sleep by GPIO.", __builtin_ctz(NRF_P0->LATCH ? NRF_P0->LATCH : NRF_P1->LATCH),
                             NRF_P0->LATCH ? 0 : 1);
                    NRF_P1->LATCH = 0xFFFFFFFF;
                    NRF_P0->LATCH = 0xFFFFFFFF;
                    sendDetectionMessage();
                } else if (regret == BOOT_FROM_TIMEOUT) {
                    LOG_INFO("Woke up by timeout.");
                    // only send if set a sleep timeout ourself. else so other module triggered it
                    if (moduleConfig.detection_sensor.state_broadcast_secs)
                        sendCurrentStateMessage(getState());
                } else if (regret == BOOT_FROM_GPIOEVENT) {
                    LOG_INFO("Woke up from interval sleep by GPIO.");
                    sendDetectionMessage();
                } else {
                    // We booted fresh. Enforce sending on first detection event.
                    lastSentToMesh = -Default::getConfiguredOrDefaultMs(moduleConfig.detection_sensor.minimum_broadcast_secs);
                }
                if (!(sd_power_gpregret_clr(0, 0xFF) == NRF_SUCCESS)) {
                    NRF_POWER->GPREGRET = BOOT_FROM_COLD;
                }

                LOG_INFO("Cold boot. Init in power saving mode");
                // Choose the minimum wakeup time (config.power.min_wake_sec) depending to your needs.
                // The least time should be 5s to send just the detection message.
                // Choose 45s+ for comfortable connectivity via BLE or USB.
                // Device Telemetry is sent after ~62s.
                return Default::getConfiguredOrDefaultMs(config.power.min_wake_secs, 90);
            } else
#elif defined(ESP32_WITH_EXT0)
            if (config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR && config.power.is_power_saving) {

                esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
                uint64_t timeNow_us;
                uint32_t timeDiff_s;

                switch (wakeCause) {
                case ESP_SLEEP_WAKEUP_EXT0:
                case ESP_SLEEP_WAKEUP_EXT1:
                    LOG_INFO("Woke up from interval sleep by GPIO. Sending detection message.");
                    timeNow_us = rtc_time_slowclk_to_us(rtc_time_get(), esp_clk_slowclk_cal_get());
                    timeDiff_s = (timeNow_us / 1000000 - sleepTime_s) + config.power.min_wake_secs;
                    // LOG_INFO("sleeptime (effectivly lastSentToMesh): %u", timeDiff_s - config.power.min_wake_secs);
                    // LOG_INFO("time since last run (effectivly lastSentToMesh): %us", timeDiff_s);

                    if (timeDiff_s + time_acc_s > moduleConfig.detection_sensor.minimum_broadcast_secs) {
                        // LOG_INFO("sending as %u > %u", timeDiff_s + time_acc_s,
                        //          moduleConfig.detection_sensor.minimum_broadcast_secs);
                        time_acc_s = 0;
                        sendDetectionMessage();
                    } else {
                        // LOG_INFO("not sending as did not reach broadcast threshold. %us < %us", timeDiff_s + time_acc_s,
                        //          moduleConfig.detection_sensor.minimum_broadcast_secs);
                        time_acc_s += timeDiff_s;
                    }
                    break;
                case ESP_SLEEP_WAKEUP_TIMER:
                    // only send if set a sleep timeout ourself. else so other module triggered it
                    LOG_INFO("Woke up by timeout.");
                    if (moduleConfig.detection_sensor.state_broadcast_secs)
                        sendCurrentStateMessage(getState());
                    break;
                case ESP_SLEEP_WAKEUP_TOUCHPAD:
                case ESP_SLEEP_WAKEUP_ULP:
                default:
                    // We booted fresh. Enforce sending on first detection event.
                    LOG_INFO("Cold boot. Init in power saving mode");
                    break;
                }

                if (rtc_gpio_is_valid_gpio((gpio_num_t)moduleConfig.detection_sensor.monitor_pin)) {
                    if (moduleConfig.detection_sensor.use_pullup) {
                        rtc_gpio_pulldown_dis((gpio_num_t)moduleConfig.detection_sensor.monitor_pin);
                        rtc_gpio_pullup_en((gpio_num_t)moduleConfig.detection_sensor.monitor_pin);
                    } else {
                        rtc_gpio_pulldown_dis((gpio_num_t)moduleConfig.detection_sensor.monitor_pin);
                        rtc_gpio_pullup_dis((gpio_num_t)moduleConfig.detection_sensor.monitor_pin);
                    }
                    if (esp_sleep_enable_ext0_wakeup((gpio_num_t)moduleConfig.detection_sensor.monitor_pin,
                                                     (moduleConfig.detection_sensor.detection_trigger_type & 1) ? 1 : 0) !=
                        ESP_OK)
                        LOG_ERROR("error enabling ext0 on gpio %d", moduleConfig.detection_sensor.monitor_pin);
                    return Default::getConfiguredOrDefaultMs(config.power.min_wake_secs, 90);
                } else {
                    LOG_WARN("Enabled, but specified pin is not an RTC pin. Disabling module");
                    printRtcPins();
                    return disable();
                }
            } else
#elif defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32H2)
            LOG_WARN("Enabled, but used ESP32 variant lacks necessary EXT0. Disabling module");
            return disable();
#endif
                pinMode(moduleConfig.detection_sensor.monitor_pin,
                        moduleConfig.detection_sensor.use_pullup ? INPUT_PULLUP : INPUT);
        } else {
            LOG_WARN("Enabled, but no monitor pin is set. Disabling module");
            return disable();
        }
        LOG_INFO("Init in default mode");

        return setStartDelay();
    }

#if defined(ARCH_NRF52) || defined(ESP32_WITH_EXT0)
    if ((config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR && config.power.is_power_saving)) {
        // If 'State Broadcast Interval' (moduleConfig.detection_sensor.state_broadcast_secs) is specified it will be used, if
        // unset the sleep will last 'forever', interrupted by specified GPIO event
        // nRF52: Using a timeout the module enters a low power loop, without it will shutdown with least power consumption
        // ESP32: Always using deep sleep with RTC
        // TODO: check why DELAY_FOREVER does not work on ESP
        uint32_t nightyNightMs =
            Default::getConfiguredOrDefault(moduleConfig.detection_sensor.state_broadcast_secs * 1000, portMAX_DELAY);

#ifdef ESP32_WITH_EXT0
        sleepTime_s = rtc_time_slowclk_to_us(rtc_time_get(), esp_clk_slowclk_cal_get()) / 1000000;
#endif
        doDeepSleep(nightyNightMs, false, true);
    }
#endif

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
            sendCurrentStateMessage(getState());
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
        sendCurrentStateMessage(getState());
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
    sprintf(message, "%s state %s, %d%%", moduleConfig.detection_sensor.name, state ? "high" : "low",
            powerStatus->getBatteryChargePercent());
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

boolean DetectionSensorModule::getState()
{
    return digitalRead(moduleConfig.detection_sensor.monitor_pin);
}

bool DetectionSensorModule::hasDetectionEvent()
{
    bool currentState = getState();
    return (moduleConfig.detection_sensor.detection_trigger_type & 1) ? currentState : !currentState;
}

#ifdef ARCH_NRF52
boolean DetectionSensorModule::shouldLoop()
{
    return moduleConfig.detection_sensor.enabled && config.power.is_power_saving && moduleConfig.detection_sensor.monitor_pin > 0;
}

void DetectionSensorModule::lpDelay()
{
    sd_power_mode_set(NRF_POWER_MODE_LOWPWR);
    delay(moduleConfig.detection_sensor.minimum_broadcast_secs - config.power.min_wake_secs);
}

void DetectionSensorModule::lpLoop(uint32_t msecToWake)
{
    for (uint32_t i = msecToWake / 100;; i--) {
        delay(100);
        if (hasDetectionEvent() &&
            !Throttle::isWithinTimespanMs(
                lastSentToMesh, Default::getConfiguredOrDefaultMs(moduleConfig.detection_sensor.minimum_broadcast_secs))) {
            if (!(sd_power_gpregret_clr(0, 0xFF) == NRF_SUCCESS &&
                  sd_power_gpregret_set(0, BOOT_FROM_GPIOEVENT) == NRF_SUCCESS)) {
                // necessary if softdevice is not enabled yet or never was
                NRF_POWER->GPREGRET = BOOT_FROM_GPIOEVENT;
            }
            break;
        }
        if (i == 0) {
            if (!(sd_power_gpregret_clr(0, 0xFF) == NRF_SUCCESS && sd_power_gpregret_set(0, BOOT_FROM_TIMEOUT) == NRF_SUCCESS)) {
                NRF_POWER->GPREGRET = BOOT_FROM_TIMEOUT;
            }
            break;
        }
    }
}
#elif defined(ESP32_WITH_EXT0)
void DetectionSensorModule::printRtcPins()
{
#if defined(CONFIG_IDF_TARGET_ESP32)
    LOG_WARN("Available GPIOs for wakeup: 0,2,4,12-15,25-27,32-39");
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
    LOG_WARN("Available GPIOs for wakeup: 0-21");
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    LOG_WARN("Available GPIOs for wakeup: 0-21");
#endif
}

bool DetectionSensorModule::skipGPIO(int gpio)
{
    return moduleConfig.detection_sensor.enabled && config.power.is_power_saving &&
           moduleConfig.detection_sensor.monitor_pin == gpio;
}
#else
bool DetectionSensorModule::skipGPIO(int gpio)
{
    // always return false for ESP32s without EXT0 (e.g ESP32c6)
    return false;
}
#endif
