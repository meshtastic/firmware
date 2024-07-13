#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "Default.h"
#include "EnvironmentTelemetry.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "Router.h"
#include "main.h"
#include "power.h"
#include "sleep.h"
#include "target_specific.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

// Sensors
#include "Sensor/AHT10.h"
#include "Sensor/BME280Sensor.h"
#include "Sensor/BME680Sensor.h"
#include "Sensor/BMP085Sensor.h"
#include "Sensor/BMP280Sensor.h"
#include "Sensor/DFRobotLarkSensor.h"
#include "Sensor/LPS22HBSensor.h"
#include "Sensor/MCP9808Sensor.h"
#include "Sensor/MLX90632Sensor.h"
#include "Sensor/NAU7802Sensor.h"
#include "Sensor/OPT3001Sensor.h"
#include "Sensor/RCWL9620Sensor.h"
#include "Sensor/SHT31Sensor.h"
#include "Sensor/SHT4XSensor.h"
#include "Sensor/SHTC3Sensor.h"
#include "Sensor/TSL2591Sensor.h"
#include "Sensor/VEML7700Sensor.h"

BMP085Sensor bmp085Sensor;
BMP280Sensor bmp280Sensor;
BME280Sensor bme280Sensor;
BME680Sensor bme680Sensor;
MCP9808Sensor mcp9808Sensor;
SHTC3Sensor shtc3Sensor;
LPS22HBSensor lps22hbSensor;
SHT31Sensor sht31Sensor;
VEML7700Sensor veml7700Sensor;
TSL2591Sensor tsl2591Sensor;
OPT3001Sensor opt3001Sensor;
SHT4XSensor sht4xSensor;
RCWL9620Sensor rcwl9620Sensor;
AHT10Sensor aht10Sensor;
MLX90632Sensor mlx90632Sensor;
DFRobotLarkSensor dfRobotLarkSensor;
NAU7802Sensor nau7802Sensor;

#define FAILED_STATE_SENSOR_READ_MULTIPLIER 10
#define DISPLAY_RECEIVEID_MEASUREMENTS_ON_SCREEN true

#include "graphics/ScreenFonts.h"

int32_t EnvironmentTelemetryModule::runOnce()
{
    if (sleepOnNextExecution == true) {
        sleepOnNextExecution = false;
        uint32_t nightyNightMs = Default::getConfiguredOrDefaultMs(moduleConfig.telemetry.environment_update_interval,
                                                                   default_telemetry_broadcast_interval_secs);
        LOG_DEBUG("Sleeping for %ims, then awaking to send metrics again.\n", nightyNightMs);
        doDeepSleep(nightyNightMs, true);
    }

    uint32_t result = UINT32_MAX;
    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
    */

    // moduleConfig.telemetry.environment_measurement_enabled = 1;
    //  moduleConfig.telemetry.environment_screen_enabled = 1;
    // moduleConfig.telemetry.environment_update_interval = 15;

    if (!(moduleConfig.telemetry.environment_measurement_enabled || moduleConfig.telemetry.environment_screen_enabled)) {
        // If this module is not enabled, and the user doesn't want the display screen don't waste any OSThread time on it
        return disable();
    }

    if (firstTime) {
        // This is the first time the OSThread library has called this function, so do some setup
        firstTime = 0;

        if (moduleConfig.telemetry.environment_measurement_enabled) {
            LOG_INFO("Environment Telemetry: Initializing\n");
            // it's possible to have this module enabled, only for displaying values on the screen.
            // therefore, we should only enable the sensor loop if measurement is also enabled
            if (dfRobotLarkSensor.hasSensor())
                result = dfRobotLarkSensor.runOnce();
            if (bmp085Sensor.hasSensor())
                result = bmp085Sensor.runOnce();
            if (bmp280Sensor.hasSensor())
                result = bmp280Sensor.runOnce();
            if (bme280Sensor.hasSensor())
                result = bme280Sensor.runOnce();
            if (bme680Sensor.hasSensor())
                result = bme680Sensor.runOnce();
            if (mcp9808Sensor.hasSensor())
                result = mcp9808Sensor.runOnce();
            if (shtc3Sensor.hasSensor())
                result = shtc3Sensor.runOnce();
            if (lps22hbSensor.hasSensor())
                result = lps22hbSensor.runOnce();
            if (sht31Sensor.hasSensor())
                result = sht31Sensor.runOnce();
            if (sht4xSensor.hasSensor())
                result = sht4xSensor.runOnce();
            if (ina219Sensor.hasSensor())
                result = ina219Sensor.runOnce();
            if (ina260Sensor.hasSensor())
                result = ina260Sensor.runOnce();
            if (ina3221Sensor.hasSensor())
                result = ina3221Sensor.runOnce();
            if (veml7700Sensor.hasSensor())
                result = veml7700Sensor.runOnce();
            if (tsl2591Sensor.hasSensor())
                result = tsl2591Sensor.runOnce();
            if (opt3001Sensor.hasSensor())
                result = opt3001Sensor.runOnce();
            if (rcwl9620Sensor.hasSensor())
                result = rcwl9620Sensor.runOnce();
            if (aht10Sensor.hasSensor())
                result = aht10Sensor.runOnce();
            if (mlx90632Sensor.hasSensor())
                result = mlx90632Sensor.runOnce();
            if (nau7802Sensor.hasSensor())
                result = nau7802Sensor.runOnce();
        }
        return result;
    } else {
        // if we somehow got to a second run of this module with measurement disabled, then just wait forever
        if (!moduleConfig.telemetry.environment_measurement_enabled) {
            return disable();
        } else {
            if (bme680Sensor.hasSensor())
                result = bme680Sensor.runTrigger();
        }

        uint32_t now = millis();
        if (((lastSentToMesh == 0) ||
             ((now - lastSentToMesh) >=
              Default::getConfiguredOrDefaultMsScaled(moduleConfig.telemetry.environment_update_interval,
                                                      default_telemetry_broadcast_interval_secs, numOnlineNodes))) &&
            airTime->isTxAllowedChannelUtil(config.device.role != meshtastic_Config_DeviceConfig_Role_SENSOR) &&
            airTime->isTxAllowedAirUtil()) {
            sendTelemetry();
            lastSentToMesh = now;
        } else if (((lastSentToPhone == 0) || ((now - lastSentToPhone) >= sendToPhoneIntervalMs)) &&
                   (service.isToPhoneQueueEmpty())) {
            // Just send to phone when it's not our time to send to mesh yet
            // Only send while queue is empty (phone assumed connected)
            sendTelemetry(NODENUM_BROADCAST, true);
            lastSentToPhone = now;
        }
    }
    return min(sendToPhoneIntervalMs, result);
}

bool EnvironmentTelemetryModule::wantUIFrame()
{
    return moduleConfig.telemetry.environment_screen_enabled;
}

float EnvironmentTelemetryModule::CelsiusToFahrenheit(float c)
{
    return (c * 9) / 5 + 32;
}

uint32_t GetTimeSinceMeshPacket(const meshtastic_MeshPacket *mp)
{
    uint32_t now = getTime();

    uint32_t last_seen = mp->rx_time;
    int delta = (int)(now - last_seen);
    if (delta < 0) // our clock must be slightly off still - not set from GPS yet
        delta = 0;

    return delta;
}

void EnvironmentTelemetryModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    if (lastMeasurementPacket == nullptr) {
        // If there's no valid packet, display "Environment"
        display->drawString(x, y, "Environment");
        display->drawString(x, y += _fontHeight(FONT_SMALL), "No measurement");
        return;
    }

    // Decode the last measurement packet
    meshtastic_Telemetry lastMeasurement;
    uint32_t agoSecs = GetTimeSinceMeshPacket(lastMeasurementPacket);
    const char *lastSender = getSenderShortName(*lastMeasurementPacket);

    auto &p = lastMeasurementPacket->decoded;
    if (!pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_Telemetry_msg, &lastMeasurement)) {
        display->drawString(x, y, "Measurement Error");
        LOG_ERROR("Unable to decode last packet");
        return;
    }

    // Display "Env. From: ..." on its own
    display->drawString(x, y, "Env. From: " + String(lastSender) + "(" + String(agoSecs) + "s)");

    String last_temp = String(lastMeasurement.variant.environment_metrics.temperature, 0) + "°C";
    if (moduleConfig.telemetry.environment_display_fahrenheit) {
        last_temp = String(CelsiusToFahrenheit(lastMeasurement.variant.environment_metrics.temperature), 0) + "°F";
    }

    // Continue with the remaining details
    display->drawString(x, y += _fontHeight(FONT_SMALL),
                        "Temp/Hum: " + last_temp + " / " +
                            String(lastMeasurement.variant.environment_metrics.relative_humidity, 0) + "%");

    if (lastMeasurement.variant.environment_metrics.barometric_pressure != 0) {
        display->drawString(x, y += _fontHeight(FONT_SMALL),
                            "Press: " + String(lastMeasurement.variant.environment_metrics.barometric_pressure, 0) + "hPA");
    }

    if (lastMeasurement.variant.environment_metrics.voltage != 0) {
        display->drawString(x, y += _fontHeight(FONT_SMALL),
                            "Volt/Cur: " + String(lastMeasurement.variant.environment_metrics.voltage, 0) + "V / " +
                                String(lastMeasurement.variant.environment_metrics.current, 0) + "mA");
    }

    if (lastMeasurement.variant.environment_metrics.iaq != 0) {
        display->drawString(x, y += _fontHeight(FONT_SMALL), "IAQ: " + String(lastMeasurement.variant.environment_metrics.iaq));
    }

    if (lastMeasurement.variant.environment_metrics.distance != 0)
        display->drawString(x, y += _fontHeight(FONT_SMALL),
                            "Water Level: " + String(lastMeasurement.variant.environment_metrics.distance, 0) + "mm");

    if (lastMeasurement.variant.environment_metrics.weight != 0)
        display->drawString(x, y += _fontHeight(FONT_SMALL),
                            "Weight: " + String(lastMeasurement.variant.environment_metrics.weight, 0) + "kg");
}

bool EnvironmentTelemetryModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Telemetry *t)
{
    if (t->which_variant == meshtastic_Telemetry_environment_metrics_tag) {
#ifdef DEBUG_PORT
        const char *sender = getSenderShortName(mp);

        LOG_INFO("(Received from %s): barometric_pressure=%f, current=%f, gas_resistance=%f, relative_humidity=%f, "
                 "temperature=%f\n",
                 sender, t->variant.environment_metrics.barometric_pressure, t->variant.environment_metrics.current,
                 t->variant.environment_metrics.gas_resistance, t->variant.environment_metrics.relative_humidity,
                 t->variant.environment_metrics.temperature);
        LOG_INFO("(Received from %s): voltage=%f, IAQ=%d, distance=%f, lux=%f\n", sender, t->variant.environment_metrics.voltage,
                 t->variant.environment_metrics.iaq, t->variant.environment_metrics.distance, t->variant.environment_metrics.lux);

        LOG_INFO("(Received from %s): wind speed=%fm/s, direction=%d degrees, weight=%fkg\n", sender,
                 t->variant.environment_metrics.wind_speed, t->variant.environment_metrics.wind_direction,
                 t->variant.environment_metrics.weight);

#endif
        // release previous packet before occupying a new spot
        if (lastMeasurementPacket != nullptr)
            packetPool.release(lastMeasurementPacket);

        lastMeasurementPacket = packetPool.allocCopy(mp);
    }

    return false; // Let others look at this message also if they want
}

bool EnvironmentTelemetryModule::getEnvironmentTelemetry(meshtastic_Telemetry *m)
{
    bool valid = true;
    bool hasSensor = false;
    m->time = getTime();
    m->which_variant = meshtastic_Telemetry_environment_metrics_tag;

    if (dfRobotLarkSensor.hasSensor()) {
        valid = valid && dfRobotLarkSensor.getMetrics(m);
        hasSensor = true;
    }
    if (sht31Sensor.hasSensor()) {
        valid = valid && sht31Sensor.getMetrics(m);
        hasSensor = true;
    }
    if (sht4xSensor.hasSensor()) {
        valid = valid && sht4xSensor.getMetrics(m);
        hasSensor = true;
    }
    if (lps22hbSensor.hasSensor()) {
        valid = valid && lps22hbSensor.getMetrics(m);
        hasSensor = true;
    }
    if (shtc3Sensor.hasSensor()) {
        valid = valid && shtc3Sensor.getMetrics(m);
        hasSensor = true;
    }
    if (bmp085Sensor.hasSensor()) {
        valid = valid && bmp085Sensor.getMetrics(m);
        hasSensor = true;
    }
    if (bmp280Sensor.hasSensor()) {
        valid = valid && bmp280Sensor.getMetrics(m);
        hasSensor = true;
    }
    if (bme280Sensor.hasSensor()) {
        valid = valid && bme280Sensor.getMetrics(m);
        hasSensor = true;
    }
    if (bme680Sensor.hasSensor()) {
        valid = valid && bme680Sensor.getMetrics(m);
        hasSensor = true;
    }
    if (mcp9808Sensor.hasSensor()) {
        valid = valid && mcp9808Sensor.getMetrics(m);
        hasSensor = true;
    }
    if (ina219Sensor.hasSensor()) {
        valid = valid && ina219Sensor.getMetrics(m);
        hasSensor = true;
    }
    if (ina260Sensor.hasSensor()) {
        valid = valid && ina260Sensor.getMetrics(m);
        hasSensor = true;
    }
    if (ina3221Sensor.hasSensor()) {
        valid = valid && ina3221Sensor.getMetrics(m);
        hasSensor = true;
    }
    if (veml7700Sensor.hasSensor()) {
        valid = valid && veml7700Sensor.getMetrics(m);
        hasSensor = true;
    }
    if (tsl2591Sensor.hasSensor()) {
        valid = valid && tsl2591Sensor.getMetrics(m);
        hasSensor = true;
    }
    if (opt3001Sensor.hasSensor()) {
        valid = valid && opt3001Sensor.getMetrics(m);
        hasSensor = true;
    }
    if (mlx90632Sensor.hasSensor()) {
        valid = valid && mlx90632Sensor.getMetrics(m);
        hasSensor = true;
    }
    if (rcwl9620Sensor.hasSensor()) {
        valid = valid && rcwl9620Sensor.getMetrics(m);
        hasSensor = true;
    }
    if (nau7802Sensor.hasSensor()) {
        valid = valid && nau7802Sensor.getMetrics(m);
        hasSensor = true;
    }
    if (aht10Sensor.hasSensor()) {
        if (!bmp280Sensor.hasSensor()) {
            valid = valid && aht10Sensor.getMetrics(m);
            hasSensor = true;
        } else {
            // prefer bmp280 temp if both sensors are present, fetch only humidity
            meshtastic_Telemetry m_ahtx = meshtastic_Telemetry_init_zero;
            LOG_INFO("AHTX0+BMP280 module detected: using temp from BMP280 and humy from AHTX0\n");
            aht10Sensor.getMetrics(&m_ahtx);
            m->variant.environment_metrics.relative_humidity = m_ahtx.variant.environment_metrics.relative_humidity;
        }
    }

    return valid && hasSensor;
}

meshtastic_MeshPacket *EnvironmentTelemetryModule::allocReply()
{
    if (currentRequest) {
        auto req = *currentRequest;
        const auto &p = req.decoded;
        meshtastic_Telemetry scratch;
        meshtastic_Telemetry *decoded = NULL;
        memset(&scratch, 0, sizeof(scratch));
        if (pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_Telemetry_msg, &scratch)) {
            decoded = &scratch;
        } else {
            LOG_ERROR("Error decoding EnvironmentTelemetry module!\n");
            return NULL;
        }
        // Check for a request for environment metrics
        if (decoded->which_variant == meshtastic_Telemetry_environment_metrics_tag) {
            meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
            if (getEnvironmentTelemetry(&m)) {
                LOG_INFO("Environment telemetry replying to request\n");
                return allocDataProtobuf(m);
            } else {
                return NULL;
            }
        }
    }
    return NULL;
}

bool EnvironmentTelemetryModule::sendTelemetry(NodeNum dest, bool phoneOnly)
{
    meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
    if (getEnvironmentTelemetry(&m)) {
        LOG_INFO("(Sending): barometric_pressure=%f, current=%f, gas_resistance=%f, relative_humidity=%f, temperature=%f\n",
                 m.variant.environment_metrics.barometric_pressure, m.variant.environment_metrics.current,
                 m.variant.environment_metrics.gas_resistance, m.variant.environment_metrics.relative_humidity,
                 m.variant.environment_metrics.temperature);
        LOG_INFO("(Sending): voltage=%f, IAQ=%d, distance=%f, lux=%f\n", m.variant.environment_metrics.voltage,
                 m.variant.environment_metrics.iaq, m.variant.environment_metrics.distance, m.variant.environment_metrics.lux);

        LOG_INFO("(Sending): wind speed=%fm/s, direction=%d degrees, weight=%fkg\n", m.variant.environment_metrics.wind_speed,
                 m.variant.environment_metrics.wind_direction, m.variant.environment_metrics.weight);

        sensor_read_error_count = 0;

        meshtastic_MeshPacket *p = allocDataProtobuf(m);
        p->to = dest;
        p->decoded.want_response = false;
        if (config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR)
            p->priority = meshtastic_MeshPacket_Priority_RELIABLE;
        else
            p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
        // release previous packet before occupying a new spot
        if (lastMeasurementPacket != nullptr)
            packetPool.release(lastMeasurementPacket);

        lastMeasurementPacket = packetPool.allocCopy(*p);
        if (phoneOnly) {
            LOG_INFO("Sending packet to phone\n");
            service.sendToPhone(p);
        } else {
            LOG_INFO("Sending packet to mesh\n");
            service.sendToMesh(p, RX_SRC_LOCAL, true);

            if (config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR && config.power.is_power_saving) {
                LOG_DEBUG("Starting next execution in 5 seconds and then going to sleep.\n");
                sleepOnNextExecution = true;
                setIntervalFromNow(5000);
            }
        }
        return true;
    }
    return false;
}

AdminMessageHandleResult EnvironmentTelemetryModule::handleAdminMessageForModule(const meshtastic_MeshPacket &mp,
                                                                                 meshtastic_AdminMessage *request,
                                                                                 meshtastic_AdminMessage *response)
{
    AdminMessageHandleResult result = AdminMessageHandleResult::NOT_HANDLED;
    if (dfRobotLarkSensor.hasSensor()) {
        result = dfRobotLarkSensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
    if (sht31Sensor.hasSensor()) {
        result = sht31Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
    if (lps22hbSensor.hasSensor()) {
        result = lps22hbSensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
    if (shtc3Sensor.hasSensor()) {
        result = shtc3Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
    if (bmp085Sensor.hasSensor()) {
        result = bmp085Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
    if (bmp280Sensor.hasSensor()) {
        result = bmp280Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
    if (bme280Sensor.hasSensor()) {
        result = bme280Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
    if (bme680Sensor.hasSensor()) {
        result = bme680Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
    if (mcp9808Sensor.hasSensor()) {
        result = mcp9808Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
    if (ina219Sensor.hasSensor()) {
        result = ina219Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
    if (ina260Sensor.hasSensor()) {
        result = ina260Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
    if (ina3221Sensor.hasSensor()) {
        result = ina3221Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
    if (veml7700Sensor.hasSensor()) {
        result = veml7700Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
    if (tsl2591Sensor.hasSensor()) {
        result = tsl2591Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
    if (opt3001Sensor.hasSensor()) {
        result = opt3001Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
    if (mlx90632Sensor.hasSensor()) {
        result = mlx90632Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
    if (rcwl9620Sensor.hasSensor()) {
        result = rcwl9620Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
    if (nau7802Sensor.hasSensor()) {
        result = nau7802Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
    if (aht10Sensor.hasSensor()) {
        result = aht10Sensor.handleAdminMessage(mp, request, response);
        if (result != AdminMessageHandleResult::NOT_HANDLED)
            return result;
    }
    return result;
}

#endif
