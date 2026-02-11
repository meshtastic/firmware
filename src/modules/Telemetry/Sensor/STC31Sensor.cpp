#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR && __has_include(<SensirionI2cStc3x.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "STC31Sensor.h"

#define STC31_NO_ERROR 0

// Binary gas configurations from STC31 datasheet
// See: setBinaryGas() - section 3.3.2 in the datasheet
#define STC31_BINARY_GAS_CO2_N2_100 0x0000  // CO2 in N2, 0-100 vol%
#define STC31_BINARY_GAS_CO2_AIR_100 0x0001 // CO2 in air, 0-100 vol%
#define STC31_BINARY_GAS_CO2_N2_25 0x0002   // CO2 in N2, 0-25 vol%
#define STC31_BINARY_GAS_CO2_AIR_25 0x0003  // CO2 in air, 0-25 vol%

STC31Sensor::STC31Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_STC31, "STC31") {}

bool STC31Sensor::configureBinaryGas()
{
    // STC31 requires binary gas mode to be set before measurements
    // It resets to default mode (no gas selected) after power cycle or reset
    LOG_DEBUG("%s: Configuring binary gas mode (CO2 in air, 0-25%%)", sensorName);
    int16_t error = stc3x.setBinaryGas(STC31_BINARY_GAS_CO2_AIR_25);
    if (error != STC31_NO_ERROR) {
        LOG_ERROR("%s: Failed to set binary gas mode, error: %d", sensorName, error);
        return false;
    }
    LOG_DEBUG("%s: Binary gas mode configured successfully", sensorName);
    binaryGasConfigured = true;
    return true;
}

bool STC31Sensor::setHumidityCompensation()
{
    // Use actual humidity from environment sensor if available, otherwise default to 50%
    float humidity = hasValidHumidity ? lastEnvironmentHumidity : 50.0f;

    LOG_DEBUG("%s: Setting humidity compensation to %.1f%%", sensorName, humidity);
    int16_t error = stc3x.setRelativeHumidity(humidity);
    if (error != STC31_NO_ERROR) {
        LOG_WARN("%s: Failed to set humidity compensation (%.1f%%), error: %d", sensorName, humidity, error);
        return false;
    }
    return true;
}

bool STC31Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s at address 0x%02x", sensorName, dev->address.address);

    _bus = bus;
    _address = dev->address.address;

    stc3x.begin(*_bus, _address);

    // Allow sensor to fully stabilize after power-on
    delay(100);

    // Run self-test to verify sensor is working
    STC3xTestResultT selfTestResult;
    int16_t testError = stc3x.selfTest(selfTestResult);
    if (testError != STC31_NO_ERROR) {
        LOG_WARN("%s: Self-test command failed with error: %d", sensorName, testError);
    } else if (selfTestResult.value != 0) {
        LOG_WARN("%s: Self-test reported error: 0x%04x", sensorName, selfTestResult.value);
    } else {
        LOG_DEBUG("%s: Self-test passed", sensorName);
    }

    // Wait after self-test before configuring
    delay(50);

    if (!configureBinaryGas())
        return false;

    setHumidityCompensation(); // Non-fatal if this fails

    status = 1;
    initI2CSensor();

    return true;
}

bool STC31Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    float gasConcentration = 0;
    float temperature = 0;

    // Binary gas mode is configured once during initDevice() and persists
    // Humidity compensation can be updated for each measurement
    setHumidityCompensation(); // Non-fatal if this fails

    // Small delay for humidity compensation to take effect
    delay(10);

    // Retry measurement up to 3 times with increasing delays
    int16_t error = STC31_NO_ERROR;
    for (int attempt = 0; attempt < 3; attempt++) {
        error = stc3x.measureGasConcentration(gasConcentration, temperature);
        if (error == STC31_NO_ERROR)
            break;
        LOG_WARN("%s: Measurement attempt %d failed (error %d), retrying...", sensorName, attempt + 1, error);

        // If we get NotEnoughDataError (0x00F in error), the sensor may have lost its config
        // Try re-configuring binary gas mode
        if ((error & 0x00F) == 0x00F && attempt == 1) {
            LOG_WARN("%s: Sensor may have lost configuration, re-configuring binary gas mode", sensorName);
            configureBinaryGas();
            delay(100);
        } else {
            delay(100 * (attempt + 1));
        }
    }

    if (error != STC31_NO_ERROR) {
        LOG_ERROR("%s: Error reading measurement after retries: %d", sensorName, error);
        // Mark that we need to re-configure on next attempt
        binaryGasConfigured = false;
        return false;
    }

    // Convert percentage to ppm: 1% = 10000 ppm
    uint32_t co2_ppm = (uint32_t)(gasConcentration * 10000.0f);

    LOG_DEBUG("%s readings: %.2f%% CO2 (=%u ppm), %.2f degC", sensorName, gasConcentration, co2_ppm, temperature);

    measurement->variant.air_quality_metrics.has_co2 = true;
    measurement->variant.air_quality_metrics.co2 = co2_ppm;

    measurement->variant.air_quality_metrics.has_co2_temperature = true;
    measurement->variant.air_quality_metrics.co2_temperature = temperature;

    return true;
}

#endif
