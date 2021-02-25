#include "EnvironmentalMeasurementPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"
#include "../mesh/generated/environmental_measurement.pb.h"
#include <DHT.h>
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

EnvironmentalMeasurementPlugin *environmentalMeasurementPlugin;
EnvironmentalMeasurementPluginRadio *environmentalMeasurementPluginRadio;

EnvironmentalMeasurementPlugin::EnvironmentalMeasurementPlugin() : concurrency::OSThread("EnvironmentalMeasurementPlugin") {}

uint32_t sensor_read_error_count = 0;

#define DHT_11_GPIO_PIN 13
#define DHT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS 1000 // Some sensors (the DHT11) have a minimum required duration between read attempts
#define FAILED_STATE_SENSOR_READ_MULTIPLIER 10
#define DISPLAY_RECEIVEID_MEASUREMENTS_ON_SCREEN true

DHT dht(DHT_11_GPIO_PIN,DHT11);


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


int32_t EnvironmentalMeasurementPlugin::runOnce() {
#ifndef NO_ESP32 // this only works on ESP32 devices

    /*
        Uncomment the preferences below if you want to use the plugin
        without having to configure it from the PythonAPI or WebUI.
    */
    /*radioConfig.preferences.environmental_measurement_plugin_measurement_enabled = 1;
    radioConfig.preferences.environmental_measurement_plugin_screen_enabled = 1;
    radioConfig.preferences.environmental_measurement_plugin_read_error_count_threshold = 5;
    radioConfig.preferences.environmental_measurement_plugin_update_interval = 30;
    radioConfig.preferences.environmental_measurement_plugin_recovery_interval = 600;*/

    if (! (radioConfig.preferences.environmental_measurement_plugin_measurement_enabled || radioConfig.preferences.environmental_measurement_plugin_screen_enabled)){
        // If this plugin is not enabled, and the user doesn't want the display screen don't waste any OSThread time on it
        return (INT32_MAX);
    }

    if (firstTime) {
        // This is the first time the OSThread library has called this function, so do some setup
        DEBUG_MSG("EnvironmentalMeasurement: Initializing\n");
        environmentalMeasurementPluginRadio = new EnvironmentalMeasurementPluginRadio();
        firstTime = 0;
        // begin reading measurements from the sensor
        // DHT have a max read-rate of 1HZ, so we should wait at least 1 second
        // after initializing the sensor before we try to read from it.
        // returning the interval here means that the next time OSThread
        // calls our plugin, we'll run the other branch of this if statement
        // and actually do a "sendOurEnvironmentalMeasurement()"
        if (radioConfig.preferences.environmental_measurement_plugin_measurement_enabled)
        {
            // it's possible to have this plugin enabled, only for displaying values on the screen.
            // therefore, we should only enable the sensor loop if measurement is also enabled
            dht.begin();
            return(DHT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);   
        }
        return (INT32_MAX);
    }
    else {
        if (!radioConfig.preferences.environmental_measurement_plugin_measurement_enabled)
        {
            // if we somehow got to a second run of this plugin with measurement disabled, then just wait forever
            // I can't imagine we'd ever get here though.
            return (INT32_MAX);
        }
        // this is not the first time OSThread library has called this function
        // so just do what we intend to do on the interval
        if(sensor_read_error_count > radioConfig.preferences.environmental_measurement_plugin_read_error_count_threshold)
        {
            if (radioConfig.preferences.environmental_measurement_plugin_recovery_interval > 0 ) {
                DEBUG_MSG(
                    "EnvironmentalMeasurement: TEMPORARILY DISABLED; The environmental_measurement_plugin_read_error_count_threshold has been exceed: %d. Will retry reads in %d seconds\n",
                    radioConfig.preferences.environmental_measurement_plugin_read_error_count_threshold,
                    radioConfig.preferences.environmental_measurement_plugin_recovery_interval);
                return(radioConfig.preferences.environmental_measurement_plugin_recovery_interval*1000);  
            }
             DEBUG_MSG(
                    "EnvironmentalMeasurement: DISABLED; The environmental_measurement_plugin_read_error_count_threshold has been exceed: %d. Reads will not be retried until after device reset\n",
                    radioConfig.preferences.environmental_measurement_plugin_read_error_count_threshold);
                return(INT32_MAX);

            
        }
        else if (sensor_read_error_count > 0){
            DEBUG_MSG("EnvironmentalMeasurement: There have been %d sensor read failures. Will retry %d more times\n", 
                sensor_read_error_count, 
                radioConfig.preferences.environmental_measurement_plugin_read_error_count_threshold-sensor_read_error_count);
        }
        if (! environmentalMeasurementPluginRadio->sendOurEnvironmentalMeasurement() ){
            // if we failed to read the sensor, then try again 
            // as soon as we can according to the maximum polling frequency
            return(DHT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);
        }
    }
    // The return of runOnce is an int32 representing the desired number of 
    // miliseconds until the function should be called again by the 
    // OSThread library.  Multiply the preference value by 1000 to convert seconds to miliseconds
    return(radioConfig.preferences.environmental_measurement_plugin_update_interval * 1000);   
#endif
}

bool EnvironmentalMeasurementPluginRadio::wantUIFrame() {
    return radioConfig.preferences.environmental_measurement_plugin_screen_enabled;
}

void EnvironmentalMeasurementPluginRadio::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_MEDIUM);
    display->drawString(x, y, "Environment");
    display->setFont(FONT_SMALL);
    display->drawString(x, y += fontHeight(FONT_MEDIUM), lastSender+": T:"+ String(lastMeasurement.temperature,2) + " H:" + String(lastMeasurement.relative_humidity,2));

}

String GetSenderName(const MeshPacket &mp) {
    String sender;

    if (nodeDB.getNode(mp.from)){
        sender = nodeDB.getNode(mp.from)->user.short_name;
    }
    else {
        sender = "UNK";
    }
    return sender;
}

bool EnvironmentalMeasurementPluginRadio::handleReceivedProtobuf(const MeshPacket &mp, const EnvironmentalMeasurement &p)
{
    if (!(radioConfig.preferences.environmental_measurement_plugin_measurement_enabled || radioConfig.preferences.environmental_measurement_plugin_screen_enabled)){
        // If this plugin is not enabled in any capacity, don't handle the packet, and allow other plugins to consume 
         return false;
    }
    bool wasBroadcast = mp.to == NODENUM_BROADCAST;

    String sender = GetSenderName(mp);
    
    // Show new nodes on LCD screen
    if (DISPLAY_RECEIVEID_MEASUREMENTS_ON_SCREEN && wasBroadcast) {
        String lcd = String("Env Measured: ") +sender + "\n" +
            "T: " + p.temperature + "\n" +
            "H: " + p.relative_humidity + "\n";
        screen->print(lcd.c_str());
    }
    DEBUG_MSG("-----------------------------------------\n");

    DEBUG_MSG("EnvironmentalMeasurement: Received data from %s\n", sender);
    DEBUG_MSG("EnvironmentalMeasurement->relative_humidity: %f\n", p.relative_humidity);
    DEBUG_MSG("EnvironmentalMeasurement->temperature: %f\n", p.temperature);

    lastMeasurement = p;
    lastSender = sender;
    return false; // Let others look at this message also if they want
}

bool EnvironmentalMeasurementPluginRadio::sendOurEnvironmentalMeasurement(NodeNum dest, bool wantReplies)
{
    EnvironmentalMeasurement m;

    m.barometric_pressure = 0; // TODO: Add support for barometric sensors
    m.relative_humidity = dht.readHumidity();
    m.temperature = dht.readTemperature();;

    DEBUG_MSG("-----------------------------------------\n");

    DEBUG_MSG("EnvironmentalMeasurement: Read data\n");
    DEBUG_MSG("EnvironmentalMeasurement->relative_humidity: %f\n", m.relative_humidity);
    DEBUG_MSG("EnvironmentalMeasurement->temperature: %f\n", m.temperature);

    if (isnan(m.relative_humidity) || isnan(m.temperature) ){
        sensor_read_error_count++;
        DEBUG_MSG("EnvironmentalMeasurement: FAILED TO READ DATA\n");
        return false;
    }

    sensor_read_error_count = 0;

    MeshPacket *p = allocDataProtobuf(m);
    p->to = dest;
    p->decoded.want_response = wantReplies;

    service.sendToMesh(p);
    return true;
}

