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
#include "MeshService.h"

// Sensors
#include "Sensor/BMP280Sensor.h"
#include "Sensor/BME280Sensor.h"
#include "Sensor/BME680Sensor.h"
#include "Sensor/MCP9808Sensor.h"
#include "Sensor/INA260Sensor.h"
#include "Sensor/INA219Sensor.h"
#include "Sensor/SHTC3Sensor.h"
#include "Sensor/LPS22HBSensor.h"
#include "Sensor/SHT31Sensor.h"

BMP280Sensor bmp280Sensor;
BME280Sensor bme280Sensor;
BME680Sensor bme680Sensor;
MCP9808Sensor mcp9808Sensor;
INA260Sensor ina260Sensor;
INA219Sensor ina219Sensor;
SHTC3Sensor shtc3Sensor;
LPS22HBSensor lps22hbSensor;
SHT31Sensor sht31Sensor;

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
            if (shtc3Sensor.hasSensor())
                result = shtc3Sensor.runOnce();
            if (lps22hbSensor.hasSensor()) {
                result = lps22hbSensor.runOnce();
            }
            if (sht31Sensor.hasSensor())
                result = sht31Sensor.runOnce();
        }
        return result;
    } else {
        // if we somehow got to a second run of this module with measurement disabled, then just wait forever
        if (!moduleConfig.telemetry.environment_measurement_enabled)
            return result;

        uint32_t now = millis();
        if ((lastSentToMesh == 0 || 
            (now - lastSentToMesh) >= getConfiguredOrDefaultMs(moduleConfig.telemetry.environment_update_interval)) && 
            airTime->channelUtilizationPercent() < max_channel_util_percent) {
            sendTelemetry();
            lastSentToMesh = now;
        } else if (service.isToPhoneQueueEmpty()) {
            // Just send to phone when it's not our time to send to mesh yet
            // Only send while queue is empty (phone assumed connected)
            sendTelemetry(NODENUM_BROADCAST, true);
        }
    }
    return sendToPhoneIntervalMs;
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
    if (!pb_decode_from_bytes(p.payload.bytes, p.payload.size, &Telemetry_msg, &lastMeasurement)) {
        display->setFont(FONT_SMALL);
        display->drawString(x, y += fontHeight(FONT_MEDIUM), "Measurement Error");
        DEBUG_MSG("Unable to decode last packet");
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

        DEBUG_MSG("(Received from %s): barometric_pressure=%f, current=%f, gas_resistance=%f, relative_humidity=%f, temperature=%f, voltage=%f\n",
            sender,
            t->variant.environment_metrics.barometric_pressure,
            t->variant.environment_metrics.current,
            t->variant.environment_metrics.gas_resistance,
            t->variant.environment_metrics.relative_humidity,
            t->variant.environment_metrics.temperature,
            t->variant.environment_metrics.voltage);

        lastMeasurementPacket = packetPool.allocCopy(mp);
    }

    return false; // Let others look at this message also if they want
}

bool EnvironmentTelemetryModule::sendTelemetry(NodeNum dest, bool phoneOnly)
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

    if (sht31Sensor.hasSensor())
        sht31Sensor.getMetrics(&m);
    if (lps22hbSensor.hasSensor())
        lps22hbSensor.getMetrics(&m);
    if (shtc3Sensor.hasSensor())
        shtc3Sensor.getMetrics(&m);
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

   DEBUG_MSG("(Sending): barometric_pressure=%f, current=%f, gas_resistance=%f, relative_humidity=%f, temperature=%f, voltage=%f\n",
        m.variant.environment_metrics.barometric_pressure,
        m.variant.environment_metrics.current,
        m.variant.environment_metrics.gas_resistance,
        m.variant.environment_metrics.relative_humidity,
        m.variant.environment_metrics.temperature,
        m.variant.environment_metrics.voltage);

    sensor_read_error_count = 0;

    MeshPacket *p = allocDataProtobuf(m);
    p->to = dest;
    p->decoded.want_response = false;
    p->priority = MeshPacket_Priority_MIN;

    lastMeasurementPacket = packetPool.allocCopy(*p);
    if (phoneOnly) {
        DEBUG_MSG("Sending packet to phone\n");
        service.sendToPhone(p);
    } else {
        DEBUG_MSG("Sending packet to mesh\n");
        service.sendToMesh(p, RX_SRC_LOCAL, true);
    }
    return true;
}
