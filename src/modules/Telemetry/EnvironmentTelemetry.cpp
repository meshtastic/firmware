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
#include "Sensor/BMP280Sensor.h"
#include "Sensor/BME280Sensor.h"
#include "Sensor/BME680Sensor.h"
#include "Sensor/MCP9808Sensor.h"
#include "Sensor/INA260Sensor.h"
#include "Sensor/INA219Sensor.h"

BMP280Sensor bmp280Sensor;
BME280Sensor bme280Sensor;
BME680Sensor bme680Sensor;
MCP9808Sensor mcp9808Sensor;
INA260Sensor ina260Sensor;
INA219Sensor ina219Sensor;

#define FAILED_STATE_SENSOR_READ_MULTIPLIER 10
#define DISPLAY_RECEIVEID_MEASUREMENTS_ON_SCREEN true

#ifdef USE_EINK
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
#ifndef ARCH_PORTDUINO
    int32_t result = INT32_MAX;
    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
    */
   
    // moduleConfig.telemetry.environment_measurement_enabled = 1;
    // moduleConfig.telemetry.environment_screen_enabled = 1;
    // moduleConfig.telemetry.environment_update_interval = 45;

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
            if (bmp280Sensor.hasSensor()) 
                result = bmp280Sensor.runOnce();
            if (bme280Sensor.hasSensor()) 
                result = bme280Sensor.runOnce();
            if (bme680Sensor.hasSensor()) 
                result = bme680Sensor.runOnce();
            if (mcp9808Sensor.hasSensor()) 
                result = mcp9808Sensor.runOnce();
            if (ina260Sensor.hasSensor()) 
                result = ina260Sensor.runOnce();
            if (ina219Sensor.hasSensor())
                result = ina219Sensor.runOnce();
        }
        return result;
    } else {
        // if we somehow got to a second run of this module with measurement disabled, then just wait forever
        if (!moduleConfig.telemetry.environment_measurement_enabled)
            return result;
        // this is not the first time OSThread library has called this function
        // so just do what we intend to do on the interval
        if (!sendOurTelemetry()) {
            // if we failed to read the sensor, then try again
            // as soon as we can according to the maximum polling frequency
            return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
        }
    }
    return getConfiguredOrDefaultMs(moduleConfig.telemetry.environment_update_interval);
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
        "Temp/Hum: " + last_temp + " / " + String(lastMeasurement.variant.environment_metrics.relative_humidity, 0) + "%");
    if (lastMeasurement.variant.environment_metrics.barometric_pressure != 0)
        display->drawString(x, y += fontHeight(FONT_SMALL),
            "Press: " + String(lastMeasurement.variant.environment_metrics.barometric_pressure, 0) + "hPA");
    if (lastMeasurement.variant.environment_metrics.voltage != 0)
        display->drawString(x, y += fontHeight(FONT_SMALL),
            "Volt/Cur: " + String(lastMeasurement.variant.environment_metrics.voltage, 0) + "V / " + String(lastMeasurement.variant.environment_metrics.current, 0) + "mA");
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

    if (bmp280Sensor.hasSensor())
        bmp280Sensor.getMetrics(&m);
    if (bme280Sensor.hasSensor())
        bme280Sensor.getMetrics(&m);
    if (bme680Sensor.hasSensor())
        bme680Sensor.getMetrics(&m);
    if (mcp9808Sensor.hasSensor())
        mcp9808Sensor.getMetrics(&m);
    if (ina219Sensor.hasSensor())
        ina219Sensor.getMetrics(&m);
    if (ina260Sensor.hasSensor())
        ina260Sensor.getMetrics(&m);

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
    service.sendToMesh(p, RX_SRC_LOCAL, true);
    return true;
}
