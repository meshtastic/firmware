#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "FSCommon.h"
#include "NAU7802Sensor.h"
#include "SafeFile.h"
#include "TelemetrySensor.h"
#include <Throttle.h>
#include <pb_decode.h>
#include <pb_encode.h>

meshtastic_Nau7802Config nau7802config = meshtastic_Nau7802Config_init_zero;

NAU7802Sensor::NAU7802Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_NAU7802, "NAU7802") {}

int32_t NAU7802Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    status = nau7802.begin(*nodeTelemetrySensorsMap[sensorType].second);
    nau7802.setSampleRate(NAU7802_SPS_320);
    if (!loadCalibrationData()) {
        LOG_ERROR("Failed to load calibration data\n");
    }
    nau7802.calibrateAFE();
    LOG_INFO("Offset: %d, Calibration factor: %.2f\n", nau7802.getZeroOffset(), nau7802.getCalibrationFactor());
    return initI2CSensor();
}

void NAU7802Sensor::setup() {}

bool NAU7802Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    LOG_DEBUG("NAU7802Sensor::getMetrics\n");
    nau7802.powerUp();
    // Wait for the sensor to become ready for one second max
    uint32_t start = millis();
    while (!nau7802.available()) {
        delay(100);
        if (!Throttle::isWithinTimespanMs(start, 1000)) {
            nau7802.powerDown();
            return false;
        }
    }
    measurement->variant.environment_metrics.has_weight = true;
    // Check if we have correct calibration values after powerup
    LOG_DEBUG("Offset: %d, Calibration factor: %.2f\n", nau7802.getZeroOffset(), nau7802.getCalibrationFactor());
    measurement->variant.environment_metrics.weight = nau7802.getWeight() / 1000; // sample is in kg
    nau7802.powerDown();
    return true;
}

void NAU7802Sensor::calibrate(float weight)
{
    nau7802.calculateCalibrationFactor(weight * 1000, 64); // internal sample is in grams
    if (!saveCalibrationData()) {
        LOG_WARN("Failed to save calibration data\n");
    }
    LOG_INFO("Offset: %d, Calibration factor: %.2f\n", nau7802.getZeroOffset(), nau7802.getCalibrationFactor());
}

AdminMessageHandleResult NAU7802Sensor::handleAdminMessage(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *request,
                                                           meshtastic_AdminMessage *response)
{
    AdminMessageHandleResult result;

    switch (request->which_payload_variant) {
    case meshtastic_AdminMessage_set_scale_tag:
        if (request->set_scale == 0) {
            this->tare();
            LOG_DEBUG("Client requested to tare scale\n");
        } else {
            this->calibrate(request->set_scale);
            LOG_DEBUG("Client requested to calibrate to %d kg\n", request->set_scale);
        }
        result = AdminMessageHandleResult::HANDLED;
        break;

    default:
        result = AdminMessageHandleResult::NOT_HANDLED;
    }

    return result;
}

void NAU7802Sensor::tare()
{
    nau7802.calculateZeroOffset(64);
    if (!saveCalibrationData()) {
        LOG_WARN("Failed to save calibration data\n");
    }
    LOG_INFO("Offset: %d, Calibration factor: %.2f\n", nau7802.getZeroOffset(), nau7802.getCalibrationFactor());
}

bool NAU7802Sensor::saveCalibrationData()
{
    auto file = SafeFile(nau7802ConfigFileName);
    nau7802config.zeroOffset = nau7802.getZeroOffset();
    nau7802config.calibrationFactor = nau7802.getCalibrationFactor();
    bool okay = false;

    LOG_INFO("%s state write to %s.\n", sensorName, nau7802ConfigFileName);
    pb_ostream_t stream = {&writecb, static_cast<Print *>(&file), meshtastic_Nau7802Config_size};

    if (!pb_encode(&stream, &meshtastic_Nau7802Config_msg, &nau7802config)) {
        LOG_ERROR("Error: can't encode protobuf %s\n", PB_GET_ERROR(&stream));
    } else {
        okay = true;
    }
    okay &= file.close();

    return okay;
}

bool NAU7802Sensor::loadCalibrationData()
{
    auto file = FSCom.open(nau7802ConfigFileName, FILE_O_READ);
    bool okay = false;
    if (file) {
        LOG_INFO("%s state read from %s.\n", sensorName, nau7802ConfigFileName);
        pb_istream_t stream = {&readcb, &file, meshtastic_Nau7802Config_size};
        if (!pb_decode(&stream, &meshtastic_Nau7802Config_msg, &nau7802config)) {
            LOG_ERROR("Error: can't decode protobuf %s\n", PB_GET_ERROR(&stream));
        } else {
            nau7802.setZeroOffset(nau7802config.zeroOffset);
            nau7802.setCalibrationFactor(nau7802config.calibrationFactor);
            okay = true;
        }
        file.close();
    } else {
        LOG_INFO("No %s state found (File: %s).\n", sensorName, nau7802ConfigFileName);
    }
    return okay;
}

#endif