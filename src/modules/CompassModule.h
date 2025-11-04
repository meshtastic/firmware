#pragma once

#include "modules/GenericThreadModule.h"
#include <QMC5883LCompass.h>
#include <Adafruit_NeoPixel.h>

class CompassModule : public GenericThreadModule
{
public:
    CompassModule();

    void setup() override;
    void loop() override;

private:
    void initCompass();
    void initLedRing();
    void updateLedRing();
    float calculateBearing(float lat1, float lon1, float lat2, float lon2);

    QMC5883LCompass compass;
    Adafruit_NeoPixel *ledRing;

    // Configuration settings
    int sdaPin;
    int sclPin;
    int ledPin;
    int ledCount;
};

extern CompassModule *compassModule;
