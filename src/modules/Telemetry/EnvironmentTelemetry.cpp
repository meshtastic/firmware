#include "EnvironmentTelemetry.h"
#include "../mesh/generated/telemetry.pb.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

// Sensors
#include "Sensor/BME280Sensor.h"
#include "Sensor/BME680Sensor.h"
#include "Sensor/DHTSensor.h"
#include "Sensor/DallasSensor.h"
#include "Sensor/MCP9808Sensor.h"

BME280Sensor bme280Sensor;
BME680Sensor bme680Sensor;
DHTSensor dhtSensor;
DallasSensor dallasSensor;
MCP9808Sensor mcp9808Sensor;

#define FAILED_STATE_SENSOR_READ_MULTIPLIER 10
#define DISPLAY_RECEIVEID_MEASUREMENTS_ON_SCREEN true

#ifdef HAS_EINK
// The screen is bigger so use bigger fonts
#define FONT_SMALL ArialMT_Plain_16
#define FONT_MEDIUM ArialMT_Plain_24
#define FONT_LARGE ArialMT_Plain_24
#else
#define FONT_SMALL ArialMT_Plain_10
#define FONT_MEDIUM ArialMT_Plain_16
#define FONT_LARGE ArialMT_Plain_24
#endif

#define fontHeight(font) ((font)[1] + 1) // height is position 1

#define FONT_HEIGHT_SMALL fontHeight(FONT_SMALL)
#define FONT_HEIGHT_MEDIUM fontHeight(FONT_MEDIUM)

int32_t EnvironmentTelemetryModule::runOnce()
{
#ifndef PORTDUINO
    int32_t result = INT32_MAX;
    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
    */
   
    /*
    moduleConfig.telemetry.environment_measurement_enabled = 1;
    moduleConfig.telemetry.environment_screen_enabled = 1;
    moduleConfig.telemetry.environment_read_error_count_threshold = 5;
    moduleConfig.telemetry.environment_update_interval = 600;
    moduleConfig.telemetry.environment_recovery_interval = 60;
    moduleConfig.telemetry.environment_sensor_pin = 13; // If one-wire
    moduleConfig.telemetry.environment_sensor_type = TelemetrySensorType::TelemetrySensorType_BME280;
    */

    if (!(moduleConfig.telemetry.environment_measurement_enabled ||
          moduleConfig.telemetry.environment_screen_enabled)) {
        // If this module is not enabled, and the user doesn't want the display screen don't waste any OSThread time on it
        return result;
    }

    if (firstTime) {
        // This is the first time the OSThread library has called this function, so do some setup
        firstTime = 0;

        if (moduleConfig.telemetry.environment_measurement_enabled) {
            DEBUG_MSG("Environment Telemetry: Initializing\n");
            // it's possible to have this module enabled, only for displaying values on the screen.
            // therefore, we should only enable the sensor loop if measurement is also enabled
            
            switch (moduleConfig.telemetry.environment_sensor_type) {
                case TelemetrySensorType_DHT11:
                case TelemetrySensorType_DHT12:
                case TelemetrySensorType_DHT21:
                case TelemetrySensorType_DHT22:
                    result = dhtSensor.runOnce();
                break;
                case TelemetrySensorType_DS18B20:
                    result = dallasSensor.runOnce();
                break;
                default:
                    DEBUG_MSG("Environment Telemetry: No sensor type specified; Checking for detected i2c sensors\n");
                break;
            }
            if (hasSensor(TelemetrySensorType_BME680)) 
                result = bme680Sensor.runOnce();
            if (hasSensor(TelemetrySensorType_BME280)) 
                result = bme280Sensor.runOnce();
            if (hasSensor(TelemetrySensorType_MCP9808)) 
                result = mcp9808Sensor.runOnce();
        }
        return result;
    } else {
        // if we somehow got to a second run of this module with measurement disabled, then just wait forever
        if (!moduleConfig.telemetry.environment_measurement_enabled)
            return result;
        // this is not the first time OSThread library has called this function
        // so just do what we intend to do on the interval
        if (sensor_read_error_count > moduleConfig.telemetry.environment_read_error_count_threshold) {
            if (moduleConfig.telemetry.environment_recovery_interval > 0) {
                DEBUG_MSG("Environment Telemetry: TEMPORARILY DISABLED; The "
                          "telemetry_module_environment_read_error_count_threshold has been exceed: %d. Will retry reads in "
                          "%d seconds\n",
                          moduleConfig.telemetry.environment_read_error_count_threshold,
                          moduleConfig.telemetry.environment_recovery_interval);
                sensor_read_error_count = 0;
                return (moduleConfig.telemetry.environment_recovery_interval * 1000);
            }
            DEBUG_MSG("Environment Telemetry: DISABLED; The telemetry_module_environment_read_error_count_threshold has "
                      "been exceed: %d. Reads will not be retried until after device reset\n",
                      moduleConfig.telemetry.environment_read_error_count_threshold);
            return result;

        } else if (sensor_read_error_count > 0) {
            DEBUG_MSG("Environment Telemetry: There have been %d sensor read failures. Will retry %d more times\n",
                      sensor_read_error_count, sensor_read_error_count, sensor_read_error_count,
                      moduleConfig.telemetry.environment_read_error_count_threshold - sensor_read_error_count);
        }
        if (!sendOurTelemetry()) {
            // if we failed to read the sensor, then try again
            // as soon as we can according to the maximum polling frequency
            return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
        }
    }
    return getIntervalOrDefaultMs(moduleConfig.telemetry.environment_update_interval);
#endif
}

uint32_t GetTimeSinceMeshPacket(const MeshPacket *mp)
{
    uint32_t now = getTime();

    uint32_t last_seen = mp->rx_time;
    int delta = (int)(now - last_seen);
    if (delta < 0) // our clock must be slightly off still - not set from GPS yet
        delta = 0;

    return delta;
}

bool EnvironmentTelemetryModule::wantUIFrame()
{
    return moduleConfig.telemetry.environment_screen_enabled;
}

float EnvironmentTelemetryModule::CelsiusToFahrenheit(float c)
{
    return (c * 9) / 5 + 32;
}

void EnvironmentTelemetryModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_MEDIUM);
    display->drawString(x, y, "Environment");
    if (lastMeasurementPacket == nullptr) {
        display->setFont(FONT_SMALL);
        display->drawString(x, y += fontHeight(FONT_MEDIUM), "No measurement");
        return;
    }

    Telemetry lastMeasurement;

    uint32_t agoSecs = GetTimeSinceMeshPacket(lastMeasurementPacket);
    const char *lastSender = getSenderShortName(*lastMeasurementPacket);

    auto &p = lastMeasurementPacket->decoded;
    if (!pb_decode_from_bytes(p.payload.bytes, p.payload.size, Telemetry_fields, &lastMeasurement)) {
        display->setFont(FONT_SMALL);
        display->drawString(x, y += fontHeight(FONT_MEDIUM), "Measurement Error");
        DEBUG_MSG("Environment Telemetry: unable to decode last packet");
        return;
    }

    display->setFont(FONT_SMALL);
    String last_temp = String(lastMeasurement.variant.environment_metrics.temperature, 0) + "°C";
    if (moduleConfig.telemetry.environment_display_fahrenheit) {
        last_temp = String(CelsiusToFahrenheit(lastMeasurement.variant.environment_metrics.temperature), 0) + "°F";
    }
    display->drawString(x, y += fontHeight(FONT_MEDIUM) - 2, "From: " + String(lastSender) + "(" + String(agoSecs) + "s)");
    display->drawString(x, y += fontHeight(FONT_SMALL) - 2,
                        "Temp/Hum: " + last_temp + " / " +
                            String(lastMeasurement.variant.environment_metrics.relative_humidity, 0) + "%");
    if (lastMeasurement.variant.environment_metrics.barometric_pressure != 0)
        display->drawString(x, y += fontHeight(FONT_SMALL),
                            "Press: " + String(lastMeasurement.variant.environment_metrics.barometric_pressure, 0) + "hPA");
}

bool EnvironmentTelemetryModule::handleReceivedProtobuf(const MeshPacket &mp, Telemetry *t)
{
    if (t->which_variant == Telemetry_environment_metrics_tag) {
        const char *sender = getSenderShortName(mp);

        DEBUG_MSG("-----------------------------------------\n");
        DEBUG_MSG("Environment Telemetry: Received data from %s\n", sender);
        DEBUG_MSG("Telemetry->time: %i\n", t->time);
        DEBUG_MSG("Telemetry->barometric_pressure: %f\n", t->variant.environment_metrics.barometric_pressure);
        DEBUG_MSG("Telemetry->current: %f\n", t->variant.environment_metrics.current);
        DEBUG_MSG("Telemetry->gas_resistance: %f\n", t->variant.environment_metrics.gas_resistance);
        DEBUG_MSG("Telemetry->relative_humidity: %f\n", t->variant.environment_metrics.relative_humidity);
        DEBUG_MSG("Telemetry->temperature: %f\n", t->variant.environment_metrics.temperature);
        DEBUG_MSG("Telemetry->voltage: %f\n", t->variant.environment_metrics.voltage);

        lastMeasurementPacket = packetPool.allocCopy(mp);
    }

    return false; // Let others look at this message also if they want
}

bool EnvironmentTelemetryModule::sendOurTelemetry(NodeNum dest, bool wantReplies)
{
    Telemetry m;
    m.time = getTime();
    m.which_variant = Telemetry_environment_metrics_tag;

    m.variant.environment_metrics.barometric_pressure = 0;
    m.variant.environment_metrics.current = 0;
    m.variant.environment_metrics.gas_resistance = 0;
    m.variant.environment_metrics.relative_humidity = 0;
    m.variant.environment_metrics.temperature = 0;
    m.variant.environment_metrics.voltage = 0;

    DEBUG_MSG("-----------------------------------------\n");
    DEBUG_MSG("Environment Telemetry: Read data\n");

    switch (moduleConfig.telemetry.environment_sensor_type) {
        case TelemetrySensorType_DS18B20:
            if (!dallasSensor.getMeasurement(&m))
                sensor_read_error_count++;
            break;
        case TelemetrySensorType_DHT11:
        case TelemetrySensorType_DHT12:
        case TelemetrySensorType_DHT21:
        case TelemetrySensorType_DHT22:
            if (!dhtSensor.getMeasurement(&m))
                sensor_read_error_count++;
            break;
        default:
            DEBUG_MSG("Environment Telemetry: No specified sensor type; Trying any detected i2c sensors\n");
    }
    if (hasSensor(TelemetrySensorType_BME280)) 
        bme280Sensor.getMeasurement(&m);
    if (hasSensor(TelemetrySensorType_BME680)) 
        bme680Sensor.getMeasurement(&m);
    if (hasSensor(TelemetrySensorType_MCP9808)) 
        mcp9808Sensor.getMeasurement(&m);

    DEBUG_MSG("Telemetry->time: %i\n", m.time);
    DEBUG_MSG("Telemetry->barometric_pressure: %f\n", m.variant.environment_metrics.barometric_pressure);
    DEBUG_MSG("Telemetry->current: %f\n", m.variant.environment_metrics.current);
    DEBUG_MSG("Telemetry->gas_resistance: %f\n", m.variant.environment_metrics.gas_resistance);
    DEBUG_MSG("Telemetry->relative_humidity: %f\n", m.variant.environment_metrics.relative_humidity);
    DEBUG_MSG("Telemetry->temperature: %f\n", m.variant.environment_metrics.temperature);
    DEBUG_MSG("Telemetry->voltage: %f\n", m.variant.environment_metrics.voltage);

    sensor_read_error_count = 0;

    MeshPacket *p = allocDataProtobuf(m);
    p->to = dest;
    p->decoded.want_response = wantReplies;

    lastMeasurementPacket = packetPool.allocCopy(*p);
    DEBUG_MSG("Environment Telemetry: Sending packet to mesh");
    service.sendToMesh(p);
    return true;
}
