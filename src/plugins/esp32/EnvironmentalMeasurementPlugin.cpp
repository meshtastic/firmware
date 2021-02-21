#include "EnvironmentalMeasurementPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"
#include "../mesh/generated/environmental_measurement.pb.h"
#include <DHT.h>

EnvironmentalMeasurementPlugin *environmentalMeasurementPlugin;
EnvironmentalMeasurementPluginRadio *environmentalMeasurementPluginRadio;

EnvironmentalMeasurementPlugin::EnvironmentalMeasurementPlugin() : concurrency::OSThread("EnvironmentalMeasurementPlugin") {}

uint32_t sensor_read_error_count = 0;

#define DHT_11_GPIO_PIN 13
//TODO: Make a related radioconfig preference to allow less-frequent reads
#define ENVIRONMENTAL_MEASUREMENT_APP_ENABLED false // DISABLED by default; set this to true if you want to use the plugin
#define DHT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS 1000
#define SENSOR_READ_ERROR_COUNT_THRESHOLD 5
#define SENSOR_READ_MULTIPLIER 3
#define FAILED_STATE_SENSOR_READ_MULTIPLIER 10
#define DISPLAY_RECEIVEID_MEASUREMENTS_ON_SCREEN true

DHT dht(DHT_11_GPIO_PIN,DHT11);

int32_t EnvironmentalMeasurementPlugin::runOnce() {
#ifndef NO_ESP32
    if (!ENVIRONMENTAL_MEASUREMENT_APP_ENABLED){
        // If this plugin is not enabled, don't waste any OSThread time on it
        return (INT32_MAX);
    }
    if (firstTime) {
        // This is the first time the OSThread library has called this function, so do some setup
        DEBUG_MSG("EnvironmentalMeasurement: Initializing as sender\n");
        environmentalMeasurementPluginRadio = new EnvironmentalMeasurementPluginRadio();
        firstTime = 0;
        // begin reading measurements from the sensor
        // DHT have a max read-rate of 1HZ, so we should wait at least 1 second
        // after initializing the sensor before we try to read from it.
        // returning the interval here means that the next time OSThread
        // calls our plugin, we'll run the other branch of this if statement
        // and actually do a "sendOurEnvironmentalMeasurement()"
        dht.begin();
        return(DHT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);   
    }
    else {
        // this is not the first time OSThread library has called this function
        // so just do what we intend to do on the interval
        if(sensor_read_error_count > SENSOR_READ_ERROR_COUNT_THRESHOLD)
        {
            DEBUG_MSG("EEnvironmentalMeasurement: DISABLED; The SENSOR_READ_ERROR_COUNT_THRESHOLD has been exceed: %d\n",SENSOR_READ_ERROR_COUNT_THRESHOLD);
            return(FAILED_STATE_SENSOR_READ_MULTIPLIER * DHT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);  
        }
        else if (sensor_read_error_count > 0){
            DEBUG_MSG("EnvironmentalMeasurement: There have been %d sensor read failures.\n",sensor_read_error_count);
        }
        if (! environmentalMeasurementPluginRadio->sendOurEnvironmentalMeasurement() ){
            // if we failed to read the sensor, then try again 
            // as soon as we can according to the maximum polling frequency
            return(DHT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);
        }
    }
    // The return of runOnce is an int32 representing the desired number of 
    // miliseconds until the function should be called again by the 
    // OSThread library.
    return(SENSOR_READ_MULTIPLIER * DHT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);   
#endif
}

bool EnvironmentalMeasurementPluginRadio::handleReceivedProtobuf(const MeshPacket &mp, const EnvironmentalMeasurement &p)
{
    if (!ENVIRONMENTAL_MEASUREMENT_APP_ENABLED){
        // If this plugin is not enabled, don't handle the packet, and allow other plugins to consume 
         return false;
    }
    bool wasBroadcast = mp.to == NODENUM_BROADCAST;
    String sender;
    
    if (nodeDB.getNode(mp.from)){
        sender = nodeDB.getNode(mp.from)->user.short_name;
    }
    else {
        sender = "UNK";
    }
    // Show new nodes on LCD screen
    if (DISPLAY_RECEIVEID_MEASUREMENTS_ON_SCREEN && wasBroadcast) {
        String lcd = String("Env Measured: ") + sender + "\n" +
            "T: " + p.temperature + "\n" +
            "H: " + p.relative_humidity + "\n";
        screen->print(lcd.c_str());
    }
    DEBUG_MSG("-----------------------------------------\n");

    DEBUG_MSG("EnvironmentalMeasurement: Received data from %s\n",sender);
    DEBUG_MSG("EnvironmentalMeasurement->relative_humidity: %f\n", p.relative_humidity);
    DEBUG_MSG("EnvironmentalMeasurement->temperature: %f\n", p.temperature);
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

