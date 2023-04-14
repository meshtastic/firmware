#include "BME680Sensor.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "FSCommon.h"
#include "TelemetrySensor.h"
#include "configuration.h"

BME680Sensor::BME680Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_BME680, "BME680") {}

int32_t BME680Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s with the BSEC Library\n", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    bme680.begin(nodeTelemetrySensorsMap[sensorType], Wire);
    if (bme680.bsecStatus == BSEC_OK) {
        bme680.setConfig(bsec_config_iaq);
        loadState();
        bme680.updateSubscription(sensorList, 13, BSEC_SAMPLE_RATE_LP);
        status = 1;
    } else {
        status = 0;
    }

    return initI2CSensor();
}

void BME680Sensor::setup() {}

bool BME680Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    bme680.run();
    measurement->variant.environment_metrics.temperature = bme680.temperature;
    measurement->variant.environment_metrics.relative_humidity = bme680.humidity;
    measurement->variant.environment_metrics.barometric_pressure = bme680.pressure / 100.0F;
    measurement->variant.environment_metrics.gas_resistance = bme680.gasResistance / 1000.0;
    // Check if we need to save state to filesystem (every STATE_SAVE_PERIOD ms)
    updateState();
    return true;
}

void BME680Sensor::loadState()
{
#ifdef FSCom
    auto file = FSCom.open(bsecConfigFileName, FILE_O_READ);
    if (file) {
        file.read((uint8_t *)&bsecState, BSEC_MAX_STATE_BLOB_SIZE);
        file.close();
        bme680.setState(bsecState);
        LOG_INFO("%s state read from %s.\n", sensorName, bsecConfigFileName);
    } else {
        LOG_INFO("No %s state found (File: %s).\n", sensorName, bsecConfigFileName);
    }
#else
    LOG_ERROR("ERROR: Filesystem not implemented\n");
#endif
}

void BME680Sensor::updateState()
{
#ifdef FSCom
    bool update = false;
    if (stateUpdateCounter == 0) {
        /* First state update when IAQ accuracy is >= 3 */
        if (bme680.iaqAccuracy >= 3) {
            LOG_DEBUG("%s state update IAQ accuracy %u >= 3\n", sensorName, bme680.iaqAccuracy);
            update = true;
            stateUpdateCounter++;
        } else {
            LOG_DEBUG("%s not updated, IAQ accuracy is %u >= 3\n", sensorName, bme680.iaqAccuracy);
        }
    } else {
        /* Update every STATE_SAVE_PERIOD minutes */
        if ((stateUpdateCounter * STATE_SAVE_PERIOD) < millis()) {
            LOG_DEBUG("%s state update every %d minutes\n", sensorName, STATE_SAVE_PERIOD);
            update = true;
            stateUpdateCounter++;
        }
    }

    if (update) {
        bme680.getState(bsecState);
        std::string filenameTmp = bsecConfigFileName;
        filenameTmp += ".tmp";
        auto file = FSCom.open(bsecConfigFileName, FILE_O_WRITE);
        if (file) {
            LOG_INFO("%s state write to %s.\n", sensorName, bsecConfigFileName);
            file.write((uint8_t *)&bsecState, BSEC_MAX_STATE_BLOB_SIZE);
            file.flush();
            file.close();
            // brief window of risk here ;-)
            if (FSCom.exists(bsecConfigFileName) && !FSCom.remove(bsecConfigFileName))
                LOG_WARN("Can't remove old state file\n");
            if (!renameFile(filenameTmp.c_str(), bsecConfigFileName))
                LOG_ERROR("Error: can't rename new state file\n");

        } else {
            LOG_INFO("Can't write %s state (File: %s).\n", sensorName, bsecConfigFileName);
        }
    }
#else
    LOG_ERROR("ERROR: Filesystem not implemented\n");
#endif
}