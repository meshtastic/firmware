#include "BME680Sensor.h"
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "FSCommon.h"
#include "TelemetrySensor.h"
#include "configuration.h"

BME680Sensor::BME680Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_BME680, "BME680") {}

int32_t BME680Sensor::runTrigger()
{
    if (!bme680.run()) {
        checkStatus("runTrigger");
    }
    return 35;
}

int32_t BME680Sensor::runOnce()
{

    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    if (!bme680.begin(nodeTelemetrySensorsMap[sensorType].first, *nodeTelemetrySensorsMap[sensorType].second))
        checkStatus("begin");

    if (bme680.status == BSEC_OK) {
        status = 1;
        if (!bme680.setConfig(bsec_config_iaq)) {
            checkStatus("setConfig");
            status = 0;
        }
        loadState();
        if (!bme680.updateSubscription(sensorList, ARRAY_LEN(sensorList), BSEC_SAMPLE_RATE_LP)) {
            checkStatus("updateSubscription");
            status = 0;
        }
        LOG_INFO("Init sensor: %s with the BSEC Library version %d.%d.%d.%d \n", sensorName, bme680.version.major,
                 bme680.version.minor, bme680.version.major_bugfix, bme680.version.minor_bugfix);
    } else {
        status = 0;
    }
    if (status == 0)
        LOG_DEBUG("BME680Sensor::runOnce: bme680.status %d\n", bme680.status);

    return initI2CSensor();
}

void BME680Sensor::setup() {}

bool BME680Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (bme680.getData(BSEC_OUTPUT_RAW_PRESSURE).signal == 0)
        return false;
    measurement->variant.environment_metrics.temperature = bme680.getData(BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE).signal;
    measurement->variant.environment_metrics.relative_humidity =
        bme680.getData(BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY).signal;
    measurement->variant.environment_metrics.barometric_pressure = bme680.getData(BSEC_OUTPUT_RAW_PRESSURE).signal / 100.0F;
    measurement->variant.environment_metrics.gas_resistance = bme680.getData(BSEC_OUTPUT_RAW_GAS).signal / 1000.0;
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
        accuracy = bme680.getData(BSEC_OUTPUT_IAQ).accuracy;
        if (accuracy >= 3) {
            LOG_DEBUG("%s state update IAQ accuracy %u >= 3\n", sensorName, accuracy);
            update = true;
            stateUpdateCounter++;
        } else {
            LOG_DEBUG("%s not updated, IAQ accuracy is %u >= 3\n", sensorName, accuracy);
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
            if (FSCom.exists(bsecConfigFileName) && !FSCom.remove(bsecConfigFileName)) {
                LOG_WARN("Can't remove old state file\n");
            }
            if (!renameFile(filenameTmp.c_str(), bsecConfigFileName)) {
                LOG_ERROR("Error: can't rename new state file\n");
            }

        } else {
            LOG_INFO("Can't write %s state (File: %s).\n", sensorName, bsecConfigFileName);
        }
    }
#else
    LOG_ERROR("ERROR: Filesystem not implemented\n");
#endif
}

void BME680Sensor::checkStatus(String functionName)
{
    if (bme680.status < BSEC_OK)
        LOG_ERROR("%s BSEC2 code: %s\n", functionName.c_str(), String(bme680.status).c_str());
    else if (bme680.status > BSEC_OK)
        LOG_WARN("%s BSEC2 code: %s\n", functionName.c_str(), String(bme680.status).c_str());

    if (bme680.sensor.status < BME68X_OK)
        LOG_ERROR("%s BME68X code: %s\n", functionName.c_str(), String(bme680.sensor.status).c_str());
    else if (bme680.sensor.status > BME68X_OK)
        LOG_WARN("%s BME68X code: %s\n", functionName.c_str(), String(bme680.sensor.status).c_str());
}
