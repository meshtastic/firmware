#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_VL53L0X.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include <memory>



#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "FSCommon.h"
#include "VL53L0XSensor.h"
#include "SPILock.h"
#include "SafeFile.h"
#include "TelemetrySensor.h"

#include <Adafruit_VL53L0X.h>
#include <typeinfo>
#include <pb_decode.h>
#include <pb_encode.h>


bool VL53L0XSensor::loadState()
{
#ifdef FSCom
    spiLock->lock();
    auto file = FSCom.open(VL53L0XStateFileName, FILE_O_READ);
    bool okay = false;
    if (file) {
        LOG_INFO("%s state read from %s", sensorName, VL53L0XStateFileName);
        pb_istream_t stream = {&readcb, &file, meshtastic_VL53L0XState_size};

        if (!pb_decode(&stream, &meshtastic_VL53L0XState_msg, &vl53state)) {
            LOG_ERROR("Error: can't decode protobuf %s", PB_GET_ERROR(&stream));
        } else {
            mode = (Adafruit_VL53L0X::VL53L0X_Sense_config_t)vl53state.mode;
            okay = true;
        }
        file.close();
    } else {
        LOG_INFO("No %s state found (File: %s)", sensorName, VL53L0XStateFileName);
    }
    spiLock->unlock();
    return okay;
#else
    LOG_ERROR("%s: ERROR - Filesystem not implemented", sensorName);
#endif
}

bool VL53L0XSensor::saveState()
{
#ifdef FSCom
    auto file = SafeFile(VL53L0XStateFileName);

    vl53state.mode = (meshtastic_VL53L0XState_RangingMode)mode;

    bool okay = false;

    LOG_INFO("%s: state write to %s", sensorName, VL53L0XStateFileName);
    pb_ostream_t stream = {&writecb, static_cast<Print *>(&file), meshtastic_VL53L0XState_size};

    if (!pb_encode(&stream, &meshtastic_VL53L0XState_msg, &vl53state)) {
        LOG_ERROR("Error: can't encode protobuf %s", PB_GET_ERROR(&stream));
    } else {
        okay = true;
    }

    okay &= file.close();

    if (okay)
        LOG_INFO("%s: state write to %s successful", sensorName, VL53L0XStateFileName);

    return okay;
#else
    LOG_ERROR("%s: ERROR - Filesystem not implemented", sensorName);
#endif
}

VL53L0XSensor::VL53L0XSensor() : TelemetrySensor(meshtastic_TelemetrySensorType_VL53L0X, "VL53L0X") {}

bool VL53L0XSensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);

    loadState();

    status = vl53l0x.begin(VL53L0X_I2C_ADDR, false, bus, mode);

    if (!status) {
        return status;
    }

    initI2CSensor();
    return status;
}

bool VL53L0XSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    VL53L0X_RangingMeasurementData_t measure;
    vl53l0x.rangingTest(&measure, false);

    measurement->variant.environment_metrics.has_distance = measure.RangeStatus == 0;

    if(measurement->variant.environment_metrics.has_distance)
        measurement->variant.environment_metrics.distance = measure.RangeMilliMeter;
    else
        measurement->variant.environment_metrics.distance = -1;

    LOG_INFO("distance %f, range status: %d", measurement->variant.environment_metrics.distance, measure.RangeStatus);

    return true;
}
#endif
