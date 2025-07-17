#include "configuration.h"
#include <cmath>
#include <cstdint>
#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_AHTX0.h>)
#include "DebugConfiguration.h"
#include "SFA30Sensor.h"
#include "modules/Telemetry/Sensor/TelemetrySensor.h"

SFA30Sensor::SFA30Sensor()
    : TelemetrySensor(meshtastic_TelemetrySensorType_SFA30, "SFA30") {};

void SFA30Sensor::setup() {}

int32_t SFA30Sensor::runOnce() {

  wire = nodeTelemetrySensorsMap[sensorType].second;
  LOG_DEBUG("SFA30 init I2C-");
  delay(10);
  uint32_t current_clock = 0;
  current_clock = wire->getClock();
  if (current_clock != SENSIRION_I2C_CLOCK) {
    wire->setClock(SENSIRION_I2C_CLOCK);
  }

  sfa30.begin(*(wire) );
  int err = sfa30.startContinuousMeasurement();
  if (err) {
    LOG_WARN("SFA30 Error startContinuousMeasurement: %i", err);
    status = -1;
  }
  // return -1;

  LOG_DEBUG("SFA30 starts measurement-");

  status = 1;
  wire->setClock(current_clock);
  return initI2CSensor();
};

bool SFA30Sensor::getMetrics(meshtastic_Telemetry *measurement) {
  LOG_DEBUG("SFA30 getMetrics");
  int16_t form = 0, hum = 0, temp = 0;

  delay(10);
  if (sfa30.readMeasuredValues(form, hum, temp))
    LOG_WARN("No SFA30 measurement- values");

  measurement->variant.air_quality_metrics.has_form_temperature = true;
  measurement->variant.air_quality_metrics.has_form_humidity = true;
  measurement->variant.air_quality_metrics.has_form_formaldehyde = true;

  measurement->variant.air_quality_metrics.form_temperature = temp / 200.0;
  measurement->variant.air_quality_metrics.form_humidity = hum / 100;
  measurement->variant.air_quality_metrics.form_formaldehyde = form / 5.0;
  LOG_DEBUG("SFA30 measurements:\n %f C; %f \%RH; %f ppb;",
            measurement->variant.air_quality_metrics.form_temperature,
            measurement->variant.air_quality_metrics.form_humidity,
            measurement->variant.air_quality_metrics.form_formaldehyde);

  return true;
}
#endif
