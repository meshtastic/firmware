/*
 *  nrf52840 internal die temperature sensor
 */

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(ARCH_NRF52)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "NRFTempSensor.h"
#include <nrf_soc.h>

#include <typeinfo>

NRFTempSensor::NRFTempSensor() {}

int32_t NRFTempSensor::runOnce()
{
    LOG_INFO("Init sensor: NRFTemp");
    return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
}

bool NRFTempSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    LOG_DEBUG("NRFTemp getMetrics");

    int32_t temp;
    if (sd_temp_get(&temp) != NRF_SUCCESS)
        return false;

    measurement->variant.environment_metrics.has_temperature = true;

    measurement->variant.environment_metrics.temperature = temp / 4.f;

    return true;
}

#endif
