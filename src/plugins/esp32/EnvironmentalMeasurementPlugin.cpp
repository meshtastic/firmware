#include "EnvironmentalMeasurementPlugin.h"
#include "../mesh/generated/environmental_measurement.pb.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"
#include <DHT.h>
#include <DS18B20.h>
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>
#include <OneWire.h>

#define DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS 1000
#define DHT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS 1000
#define DS18B20_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS 1000
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
#ifndef NO_ESP32 // this only works on ESP32 devices

    /*
        Uncomment the preferences below if you want to use the plugin
        without having to configure it from the PythonAPI or WebUI.
    */

    /*radioConfig.preferences.environmental_measurement_plugin_measurement_enabled = 1;
    radioConfig.preferences.environmental_measurement_plugin_screen_enabled = 1;
    radioConfig.preferences.environmental_measurement_plugin_read_error_count_threshold = 5;
    radioConfig.preferences.environmental_measurement_plugin_update_interval = 600;
    radioConfig.preferences.environmental_measurement_plugin_recovery_interval = 60;
    radioConfig.preferences.environmental_measurement_plugin_display_farenheit = false;
    radioConfig.preferences.environmental_measurement_plugin_sensor_pin = 13;
    radioConfig.preferences.environmental_measurement_plugin_sensor_type =
        RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType::
            RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DS18B20;
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
                dht = new DHT(radioConfig.preferences.environmental_measurement_plugin_sensor_pin, DHT11);
                this->dht->begin();
                this->dht->read();
                DEBUG_MSG("EnvironmentalMeasurement: Opened DHT11 on pin: %d\n",
                          radioConfig.preferences.environmental_measurement_plugin_sensor_pin);
                return (DHT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);
            case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DS18B20:
                oneWire = new OneWire(radioConfig.preferences.environmental_measurement_plugin_sensor_pin);
                ds18b20 = new DS18B20(oneWire);
                this->ds18b20->begin();
                this->ds18b20->setResolution(12);
                this->ds18b20->requestTemperatures();
                DEBUG_MSG("EnvironmentalMeasurement: Opened DS18B20 on pin: %d\n",
                          radioConfig.preferences.environmental_measurement_plugin_sensor_pin);
                return (DS18B20_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);
            default:
                DEBUG_MSG("EnvironmentalMeasurement: Invalid sensor type selected; Disabling plugin");
                return (INT32_MAX);
                break;
            }
        }
        return (INT32_MAX);
    } else {
        if (!radioConfig.preferences.environmental_measurement_plugin_measurement_enabled) {
            // if we somehow got to a second run of this plugin with measurement disabled, then just wait forever
            // I can't imagine we'd ever get here though.
            return (INT32_MAX);
        }
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
            // return (DHT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);

            switch (radioConfig.preferences.environmental_measurement_plugin_sensor_type) {
            case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DHT11:
                return (DHT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);
            case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DS18B20:
                return (DS18B20_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);
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
        ;
    }

    display->drawString(x, y += fontHeight(FONT_MEDIUM),
                        lastSender + ": " + last_temp + "/" + String(lastMeasurement.relative_humidity, 0) + "%(" +
                            String(agoSecs) + "s)");
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

    lastMeasurementPacket = packetPool.allocCopy(mp);

    return false; // Let others look at this message also if they want
}

bool EnvironmentalMeasurementPlugin::sendOurEnvironmentalMeasurement(NodeNum dest, bool wantReplies)
{
    EnvironmentalMeasurement m;

    m.barometric_pressure = 0; // TODO: Add support for barometric sensors
    DEBUG_MSG("-----------------------------------------\n");

    DEBUG_MSG("EnvironmentalMeasurement: Read data\n");

    switch (radioConfig.preferences.environmental_measurement_plugin_sensor_type) {
    case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DHT11:
        if (!this->dht->read(true)) {
            sensor_read_error_count++;
            DEBUG_MSG("EnvironmentalMeasurement: FAILED TO READ DATA\n");
            return false;
        }
        m.relative_humidity = this->dht->readHumidity();
        m.temperature = this->dht->readTemperature();
        break;
    case RadioConfig_UserPreferences_EnvironmentalMeasurementSensorType_DS18B20:
        if (this->ds18b20->isConversionComplete()) {
            m.temperature = this->ds18b20->getTempC();
            m.relative_humidity = 0; // This sensor is temperature only
            this->ds18b20->requestTemperatures();
            break;
        } else {
            sensor_read_error_count++;
            DEBUG_MSG("EnvironmentalMeasurement: FAILED TO READ DATA\n");
            return false;
        }
    default:
        DEBUG_MSG("EnvironmentalMeasurement: Invalid sensor type selected; Disabling plugin");
        return false;
    }

    DEBUG_MSG("EnvironmentalMeasurement->relative_humidity: %f\n", m.relative_humidity);
    DEBUG_MSG("EnvironmentalMeasurement->temperature: %f\n", m.temperature);

    sensor_read_error_count = 0;

    MeshPacket *p = allocDataProtobuf(m);
    p->to = dest;
    p->decoded.want_response = wantReplies;

    service.sendToMesh(p);
    return true;
}
