#include "EnvironmentalMeasurementPlugin.h"
#include "../mesh/generated/environmental_measurement.pb.h"
#include "MeshService.h"
#include "NodeDB.h"
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

BME280Sensor bme280Sensor;
BME680Sensor bme680Sensor;
DHTSensor dhtSensor;
DallasSensor dallasSensor;

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


int32_t EnvironmentalMeasurementPlugin::runOnce()
{
#ifndef PORTDUINO
    /*
        Uncomment the preferences below if you want to use the plugin
        without having to configure it from the PythonAPI or WebUI.
    */
    /*
    radioConfig.preferences.environmental_measurement_plugin_measurement_enabled = 1;
    radioConfig.preferences.environmental_measurement_plugin_screen_enabled = 1;
    radioConfig.preferences.environmental_measurement_plugin_read_error_count_threshold = 5;
    radioConfig.preferences.environmental_measurement_plugin_update_interval = 600;
    radioConfig.preferences.environmental_measurement_plugin_recovery_interval = 60;
    radioConfig.preferences.environmental_measurement_plugin_display_farenheit = false;
    radioConfig.preferences.environmental_measurement_plugin_sensor_pin = 13;

    radioConfig.preferences.environmental_measurement_plugin_sensor_type =
        RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType::
            RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_BME280;
    */
   
    if (!(radioConfig.preferences.environmental_measurement_plugin_measurement_enabled ||
          radioConfig.preferences.environmental_measurement_plugin_screen_enabled)) {
        // If this plugin is not enabled, and the user doesn't want the display screen don't waste any OSThread time on it
        return (INT32_MAX);
    }

    if (firstTime) {
        // This is the first time the OSThread library has called this function, so do some setup
        firstTime = 0;

        if (radioConfig.preferences.environmental_measurement_plugin_measurement_enabled) {
            DEBUG_MSG("EnvironmentalMeasurement: Initializing\n");
            // it's possible to have this plugin enabled, only for displaying values on the screen.
            // therefore, we should only enable the sensor loop if measurement is also enabled
            switch (radioConfig.preferences.environmental_measurement_plugin_sensor_type) {

                case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DHT11:
                case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DHT12:
                case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DHT21:
                case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DHT22:
                    return dhtSensor.runOnce();
                case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DS18B20:
                    return dallasSensor.runOnce();
                case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_BME280:
                    return bme280Sensor.runOnce();
                case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_BME680:
                    return bme680Sensor.runOnce();
                default:
                    DEBUG_MSG("EnvironmentalMeasurement: Invalid sensor type selected; Disabling plugin");
                    return (INT32_MAX);
                    break;
            }
        }
        return (INT32_MAX);
    } else {
        // if we somehow got to a second run of this plugin with measurement disabled, then just wait forever
        if (!radioConfig.preferences.environmental_measurement_plugin_measurement_enabled)  
            return (INT32_MAX);
        // this is not the first time OSThread library has called this function
        // so just do what we intend to do on the interval
        if (sensor_read_error_count > radioConfig.preferences.environmental_measurement_plugin_read_error_count_threshold) {
            if (radioConfig.preferences.environmental_measurement_plugin_recovery_interval > 0) {
                DEBUG_MSG("EnvironmentalMeasurement: TEMPORARILY DISABLED; The "
                          "environmental_measurement_plugin_read_error_count_threshold has been exceed: %d. Will retry reads in "
                          "%d seconds\n",
                          radioConfig.preferences.environmental_measurement_plugin_read_error_count_threshold,
                          radioConfig.preferences.environmental_measurement_plugin_recovery_interval);
                sensor_read_error_count = 0;
                return (radioConfig.preferences.environmental_measurement_plugin_recovery_interval * 1000);
            }
            DEBUG_MSG("EnvironmentalMeasurement: DISABLED; The environmental_measurement_plugin_read_error_count_threshold has "
                      "been exceed: %d. Reads will not be retried until after device reset\n",
                      radioConfig.preferences.environmental_measurement_plugin_read_error_count_threshold);
            return (INT32_MAX);

        } else if (sensor_read_error_count > 0) {
            DEBUG_MSG("EnvironmentalMeasurement: There have been %d sensor read failures. Will retry %d more times\n",
                      sensor_read_error_count, sensor_read_error_count, sensor_read_error_count,
                      radioConfig.preferences.environmental_measurement_plugin_read_error_count_threshold -
                          sensor_read_error_count);
        }
        if (!sendOurEnvironmentalMeasurement()) {
            // if we failed to read the sensor, then try again
            // as soon as we can according to the maximum polling frequency

            switch (radioConfig.preferences.environmental_measurement_plugin_sensor_type) {
                case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DHT11:
                case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DHT12:
                case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DHT21:
                case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DHT22:
                    return (DHT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);
                case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DS18B20:
                    return (DS18B20_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);
                case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_BME280:
                case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_BME680:
                    return (BME_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);
                default:
                    return (DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);
            }
        }
    }
    // The return of runOnce is an int32 representing the desired number of
    // miliseconds until the function should be called again by the
    // OSThread library.  Multiply the preference value by 1000 to convert seconds to miliseconds
    return (radioConfig.preferences.environmental_measurement_plugin_update_interval * 1000);
#endif
}

bool EnvironmentalMeasurementPlugin::wantUIFrame()
{
    return radioConfig.preferences.environmental_measurement_plugin_screen_enabled;
}

String GetSenderName(const MeshPacket &mp)
{
    String sender;

    auto node = nodeDB.getNode(getFrom(&mp));
    if (node) {
        sender = node->user.short_name;
    } else {
        sender = "UNK";
    }
    return sender;
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

float EnvironmentalMeasurementPlugin::CelsiusToFarenheit(float c)
{
    return (c * 9) / 5 + 32;
}

void EnvironmentalMeasurementPlugin::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_MEDIUM);
    display->drawString(x, y, "Environment");
    if (lastMeasurementPacket == nullptr) {
        display->setFont(FONT_SMALL);
        display->drawString(x, y += fontHeight(FONT_MEDIUM), "No measurement");
        return;
    }

    EnvironmentalMeasurement lastMeasurement;

    uint32_t agoSecs = GetTimeSinceMeshPacket(lastMeasurementPacket);
    String lastSender = GetSenderName(*lastMeasurementPacket);

    auto &p = lastMeasurementPacket->decoded;
    if (!pb_decode_from_bytes(p.payload.bytes, p.payload.size, EnvironmentalMeasurement_fields, &lastMeasurement)) {
        display->setFont(FONT_SMALL);
        display->drawString(x, y += fontHeight(FONT_MEDIUM), "Measurement Error");
        DEBUG_MSG("EnvironmentalMeasurement: unable to decode last packet");
        return;
    }

    display->setFont(FONT_SMALL);
    String last_temp = String(lastMeasurement.temperature, 0) + "°C";
    if (radioConfig.preferences.environmental_measurement_plugin_display_farenheit) {
        last_temp = String(CelsiusToFarenheit(lastMeasurement.temperature), 0) + "°F";
    }
    display->drawString(x, y += fontHeight(FONT_MEDIUM) - 2, "From: " + lastSender + "(" + String(agoSecs) + "s)");
    display->drawString(x, y += fontHeight(FONT_SMALL) - 2,"Temp/Hum: " + last_temp + " / " + String(lastMeasurement.relative_humidity, 0) + "%");
    if (lastMeasurement.barometric_pressure != 0) 
        display->drawString(x, y += fontHeight(FONT_SMALL),"Press: " + String(lastMeasurement.barometric_pressure, 0) + "hPA");
}

bool EnvironmentalMeasurementPlugin::handleReceivedProtobuf(const MeshPacket &mp, EnvironmentalMeasurement *p)
{
    if (!(radioConfig.preferences.environmental_measurement_plugin_measurement_enabled ||
          radioConfig.preferences.environmental_measurement_plugin_screen_enabled)) {
        // If this plugin is not enabled in any capacity, don't handle the packet, and allow other plugins to consume
        return false;
    }

    String sender = GetSenderName(mp);

    DEBUG_MSG("EnvironmentalMeasurement: Received data from %s\n", sender);
    DEBUG_MSG("EnvironmentalMeasurement->relative_humidity: %f\n", p->relative_humidity);
    DEBUG_MSG("EnvironmentalMeasurement->temperature: %f\n", p->temperature);
    DEBUG_MSG("EnvironmentalMeasurement->barometric_pressure: %f\n", p->barometric_pressure);
    DEBUG_MSG("EnvironmentalMeasurement->gas_resistance: %f\n", p->gas_resistance);

    lastMeasurementPacket = packetPool.allocCopy(mp);

    return false; // Let others look at this message also if they want
}

bool EnvironmentalMeasurementPlugin::sendOurEnvironmentalMeasurement(NodeNum dest, bool wantReplies)
{
    EnvironmentalMeasurement m;
    m.barometric_pressure = 0;
    m.gas_resistance = 0;
    DEBUG_MSG("-----------------------------------------\n");

    DEBUG_MSG("EnvironmentalMeasurement: Read data\n");

    switch (radioConfig.preferences.environmental_measurement_plugin_sensor_type) {
        case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DS18B20:
            if (!dallasSensor.getMeasurement(&m))
                sensor_read_error_count++;
            break;
        case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DHT11:
        case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DHT12:
        case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DHT21:
        case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DHT22:
            if (!dhtSensor.getMeasurement(&m))
                sensor_read_error_count++;
            break;
        case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_BME280:
            bme280Sensor.getMeasurement(&m);
            break;
        case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_BME680:
            bme680Sensor.getMeasurement(&m);
            break;
        default:
            DEBUG_MSG("EnvironmentalMeasurement: Invalid sensor type selected; Disabling plugin");
            return false;
    }

    DEBUG_MSG("EnvironmentalMeasurement->relative_humidity: %f\n", m.relative_humidity);
    DEBUG_MSG("EnvironmentalMeasurement->temperature: %f\n", m.temperature);
    DEBUG_MSG("EnvironmentalMeasurement->barometric_pressure: %f\n", m.barometric_pressure);
    DEBUG_MSG("EnvironmentalMeasurement->gas_resistance: %f\n", m.gas_resistance);

    sensor_read_error_count = 0;

    MeshPacket *p = allocDataProtobuf(m);
    p->to = dest;
    p->decoded.want_response = wantReplies;

    lastMeasurementPacket = packetPool.allocCopy(*p);
    DEBUG_MSG("EnvironmentalMeasurement: Sending packet to mesh");
    service.sendToMesh(p);
    return true;
}
