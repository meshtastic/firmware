#include "CompassModule.h"
#include "mesh/MeshTypes.h"
#include "mesh/NodeDB.h"
#include "wiring.h"
#include "platform.h"
#include "globals.h"

CompassModule *compassModule;

CompassModule::CompassModule()
{
    compassModule = this;
}

void CompassModule::setup()
{
    // Initialize configuration settings
    sdaPin = COMPASS_SDA_PIN;
    sclPin = COMPASS_SCL_PIN;
    ledPin = COMPASS_LED_PIN;
    ledCount = COMPASS_LED_COUNT;

    initCompass();
    initLedRing();
}

void CompassModule::loop()
{
    compass.read();
    LOG_DEBUG("Compass heading: %d", compass.getAzimuth());
    updateLedRing();
}

void CompassModule::initCompass()
{
    Wire.begin(sdaPin, sclPin);
    compass.init();
    LOG_INFO("Compass initialized");
}

void CompassModule::initLedRing()
{
    ledRing = new Adafruit_NeoPixel(ledCount, ledPin, NEO_GRB + NEO_KHZ800);
    ledRing->begin();
    ledRing->show(); // Initialize all pixels to 'off'
    LOG_INFO("LED ring initialized");
}

void CompassModule::updateLedRing()
{
    ledRing->clear();

    if (!globals.position.latitude || !globals.position.longitude)
        return; // No local position yet

    float localLat = globals.position.latitude;
    float localLon = globals.position.longitude;
    float heading = compass.getAzimuth();

    for (auto const &[nodeNum, nodeInfo] : nodeDB->nodes)
    {
        if (nodeInfo->position.latitude && nodeInfo->position.longitude)
        {
            float neighborLat = nodeInfo->position.latitude;
            float neighborLon = nodeInfo->position.longitude;

            float bearing = calculateBearing(localLat, localLon, neighborLat, neighborLon);
            float relativeBearing = fmod((bearing - heading + 360), 360);

            LOG_DEBUG("Node %d: bearing=%f, relativeBearing=%f", nodeNum, bearing, relativeBearing);

            int ledIndex = (int)(relativeBearing / (360.0 / ledCount));
            ledRing->setPixelColor(ledIndex, ledRing->Color(255, 0, 0)); // Red for now
        }
    }
    ledRing->show();
}

#include <math.h>

float CompassModule::calculateBearing(float lat1, float lon1, float lat2, float lon2)
{
    float dLon = lon2 - lon1;
    float y = sin(dLon) * cos(lat2);
    float x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dLon);
    float bearing = atan2(y, x);
    return fmod((bearing * 180 / M_PI + 360), 360);
}
