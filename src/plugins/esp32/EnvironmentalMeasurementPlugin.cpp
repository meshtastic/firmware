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
#define DHT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS 1000
#define SENSOR_READ_ERROR_COUNT_THRESHOLD 5
#define SENSOR_READ_MULTIPLIER 3


DHT dht(DHT_11_GPIO_PIN,DHT11);

int32_t EnvironmentalMeasurementPlugin::runOnce() {
#ifndef NO_ESP32
    if (firstTime) {
        // This is the first time the OSThread library has called this function, so do some setup
        DEBUG_MSG("Initializing Environmental Measurement Plugin -- Sender\n");
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
            DEBUG_MSG("Environmental Measurement Plugin: DISABLED; The SENSOR_READ_ERROR_COUNT_THRESHOLD has been exceed: %d\n",SENSOR_READ_ERROR_COUNT_THRESHOLD);
            return(DHT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS);  
        }
        else if (sensor_read_error_count > 0){
            DEBUG_MSG("Environmental Measurement Plugin: There have been %d sensor read failures.\n",sensor_read_error_count);
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
    // This plugin doesn't really do anything with the messages it receives.
    return false; // Let others look at this message also if they want
}

bool EnvironmentalMeasurementPluginRadio::sendOurEnvironmentalMeasurement(NodeNum dest, bool wantReplies)
{
    EnvironmentalMeasurement m;

    m.barometric_pressure=0;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    m.relative_humidity= 0;
    m.temperature=0;

    DEBUG_MSG("-----------------------------------------\n");

    DEBUG_MSG("Environmental Measurement Plugin: Read data\n");
    DEBUG_MSG("EnvironmentalMeasurement->relative_humidity: %f\n",h);
    DEBUG_MSG("EnvironmentalMeasurement->temperature: %f\n",t);

    if (isnan(h) || isnan(t) ){
        sensor_read_error_count++;
        DEBUG_MSG("Environmental Measurement Plugin: FAILED TO READ DATA\n");
        return false;
    }

    sensor_read_error_count = 0;

    MeshPacket *p = allocDataProtobuf(m);
    p->to = dest;
    p->decoded.want_response = wantReplies;

    service.sendToMesh(p);
    return true;
}

