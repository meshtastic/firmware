#include "BMX160Sensor.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

BMX160Sensor::BMX160Sensor(ScanI2C::DeviceAddress address) : MotionSensor::MotionSensor(ScanI2C::DeviceType::BMX160, address) {}

#ifdef RAK_4631

bool BMX160Sensor::init()
{
    if (sensor.begin()) {
        // set output data rate
        sensor.ODR_Config(BMX160_ACCEL_ODR_100HZ, BMX160_GYRO_ODR_100HZ);
        LOG_DEBUG("BMX160Sensor::init ok\n");
        return true;
    }
    LOG_DEBUG("BMX160Sensor::init failed\n");
    return false;
}

int32_t BMX160Sensor::runOnce()
{
    sBmx160SensorData_t magAccel;
    sBmx160SensorData_t gAccel;

    /* Get a new sensor event */
    sensor.getAllData(&magAccel, NULL, &gAccel);

    // expirimental calibrate routine. Limited to between 10 and 30 seconds after boot
    if (millis() > 12 * 1000 && millis() < 30 * 1000) {
        if (!showingScreen) {
            showingScreen = true;
            screen->startAlert((FrameCallback)drawFrameCalibration);
        }
        if (magAccel.x > highestX)
            highestX = magAccel.x;
        if (magAccel.x < lowestX)
            lowestX = magAccel.x;
        if (magAccel.y > highestY)
            highestY = magAccel.y;
        if (magAccel.y < lowestY)
            lowestY = magAccel.y;
        if (magAccel.z > highestZ)
            highestZ = magAccel.z;
        if (magAccel.z < lowestZ)
            lowestZ = magAccel.z;
    } else if (showingScreen && millis() >= 30 * 1000) {
        showingScreen = false;
        screen->endAlert();
    }

    int highestRealX = highestX - (highestX + lowestX) / 2;

    magAccel.x -= (highestX + lowestX) / 2;
    magAccel.y -= (highestY + lowestY) / 2;
    magAccel.z -= (highestZ + lowestZ) / 2;
    FusionVector ga, ma;
    ga.axis.x = -gAccel.x; // default location for the BMX160 is on the rear of the board
    ga.axis.y = -gAccel.y;
    ga.axis.z = gAccel.z;
    ma.axis.x = -magAccel.x;
    ma.axis.y = -magAccel.y;
    ma.axis.z = magAccel.z * 3;

    // If we're set to one of the inverted positions
    if (config.display.compass_orientation > meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_270) {
        ma = FusionAxesSwap(ma, FusionAxesAlignmentNXNYPZ);
        ga = FusionAxesSwap(ga, FusionAxesAlignmentNXNYPZ);
    }

    float heading = FusionCompassCalculateHeading(FusionConventionNed, ga, ma);

    switch (config.display.compass_orientation) {
    case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_0_INVERTED:
    case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_0:
        break;
    case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_90:
    case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_90_INVERTED:
        heading += 90;
        break;
    case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_180:
    case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_180_INVERTED:
        heading += 180;
        break;
    case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_270:
    case meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_270_INVERTED:
        heading += 270;
        break;
    }
    screen->setHeading(heading);

    return ACCELEROMETER_CHECK_INTERVAL_MS;
}

void BMX160Sensor::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    int x_offset = display->width() / 2;
    int y_offset = display->height() <= 80 ? 0 : 32;
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_MEDIUM);
    display->drawString(x, y, "Calibrating\nCompass");
    int16_t compassX = 0, compassY = 0;
    uint16_t compassDiam = graphics::Screen::getCompassDiam(display->getWidth(), display->getHeight());

    // coordinates for the center of the compass/circle
    if (config.display.displaymode == meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT) {
        compassX = x + display->getWidth() - compassDiam / 2 - 5;
        compassY = y + display->getHeight() / 2;
    } else {
        compassX = x + display->getWidth() - compassDiam / 2 - 5;
        compassY = y + FONT_HEIGHT_SMALL + (display->getHeight() - FONT_HEIGHT_SMALL) / 2;
    }
    display->drawCircle(compassX, compassY, compassDiam / 2);
    screen->drawCompassNorth(display, compassX, compassY, screen->getHeading() * PI / 180);
}

#endif

#endif