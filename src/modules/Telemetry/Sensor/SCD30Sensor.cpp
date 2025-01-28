#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "SCD30Sensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_SCD30.h>
#include <typeinfo>

SCD30Sensor::SCD30Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_SCD30, "SCD30") {}

int32_t SCD30Sensor::runOnce()
{
  if(!scd30.begin())
  {
    scd30.begin();
  }
  scd30.setMeasurementInterval(10);
  scd30.startContinuousMeasurement();
  delay(1000); // Wait 2 seconds for initialization and calibration
  LOG_INFO("SCD30 setup:");
  LOG_INFO("%f, seconds",scd30.getMeasurementInterval());
}

void SCD30Sensor::setup() {
}

bool SCD30Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
  measurement->variant.environment_metrics.has_temperature = true;
  measurement->variant.environment_metrics.has_relative_humidity = true;
  measurement->variant.air_quality_metrics.has_co2 = true;
  if (!scd30.dataReady())
  {
    LOG_DEBUG("scd30 data not ready yet, delaying");
    delay(2000); 
    return false;
  }
  if (!scd30.read()) {
      LOG_DEBUG("SCD30 read failed!");
      delay(500);
      return false;
  }
  measurement->variant.environment_metrics.temperature = scd30.temperature;
  LOG_INFO("SCD30 Temperature: %0.2f degrees C", scd30.temperature);
  measurement->variant.environment_metrics.relative_humidity = scd30.relative_humidity;
  LOG_INFO("SCD30 Relative Humidity: %0.2f %", scd30.relative_humidity);
  measurement->variant.air_quality_metrics.co2 = scd30.CO2;
  LOG_INFO("SCD30 CO2: %0.2f ppm", scd30.CO2);
  return true;
}
#endif