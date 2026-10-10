#include "BMM150Sensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<DFRobot_BMM150.h>)
#include "detect/ScanI2CTwoWire.h"
#if !defined(MESHTASTIC_EXCLUDE_SCREEN)

// screen is defined in main.cpp
extern std::unique_ptr<graphics::Screen> screen;
#endif

static constexpr float BMM150_MIN_AXIS_RADIUS = 1.0f;   // uT; the driver reports whole microtesla
static constexpr int32_t BMM150_POLL_INTERVAL_MS = 200; // each read blocks the main loop for the library's 3 ms delay

BMM150Sensor::BMM150Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

bool BMM150Sensor::init()
{
    // Initialise the sensor
    sensor = BMM150Singleton::GetInstance(device);
    if (!sensor->init(device)) {
        return false;
    }

    loadMagnetometerCalibration(compassCalibrationFileName, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
    LOG_DEBUG("BMM150 calibration extrema: X=(%.0f, %.0f), Y=(%.0f, %.0f), Z=(%.0f, %.0f)", lowestX, highestX, lowestY, highestY,
              lowestZ, highestZ);
    return true;
}

bool BMM150Sensor::readMagnetometer(float &xMicroTesla, float &yMicroTesla, float &zMicroTesla)
{
    const sBmm150MagData_t data = sensor->getGeomagneticData();
    // An ADC overflow reads as -32768; one such sample would wreck the calibration extrema
    if (data.x == BMM150_OVERFLOW_OUTPUT || data.y == BMM150_OVERFLOW_OUTPUT || data.z == BMM150_OVERFLOW_OUTPUT) {
        return false;
    }

    xMicroTesla = data.x;
    yMicroTesla = data.y;
    zMicroTesla = data.z;
    return true;
}

int32_t BMM150Sensor::runOnce()
{
#if !defined(MESHTASTIC_EXCLUDE_SCREEN) && HAS_SCREEN
    float magX = 0, magY = 0, magZ = 0;
    if (!readMagnetometer(magX, magY, magZ)) {
        return BMM150_POLL_INTERVAL_MS;
    }

    if (doCalibration) {
        beginCalibrationDisplay(showingScreen);
        if (seedPending) {
            seedCalibrationExtrema(magX, magY, magZ, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
            seedPending = false;
        }
        updateCalibrationExtrema(magX, magY, magZ, highestX, lowestX, highestY, lowestY, highestZ, lowestZ);
        finishCalibrationIfExpired(showingScreen, compassCalibrationFileName, highestX, lowestX, highestY, lowestY, highestZ,
                                   lowestZ);
    }

    // Hard-iron bias removal.
    magX -= (highestX + lowestX) * 0.5f;
    magY -= (highestY + lowestY) * 0.5f;
    magZ -= (highestZ + lowestZ) * 0.5f;

    // Soft-iron diagonal scaling from calibration extrema; also cancels the driver's X/Y gain mismatch.
    const float radiusX = (highestX - lowestX) * 0.5f;
    const float radiusY = (highestY - lowestY) * 0.5f;
    const float radiusZ = (highestZ - lowestZ) * 0.5f;
    const float avgRadius = (radiusX + radiusY + radiusZ) / 3.0f;
    magX *= (radiusX > BMM150_MIN_AXIS_RADIUS) ? (avgRadius / radiusX) : 1.0f;
    magY *= (radiusY > BMM150_MIN_AXIS_RADIUS) ? (avgRadius / radiusY) : 1.0f;
    magZ *= (radiusZ > BMM150_MIN_AXIS_RADIUS) ? (avgRadius / radiusZ) : 1.0f;

    // Publish the calibrated magnetometer values for the optional on-screen debug readout.
    publishCompassMagSample(magX, magY, magZ);

    // Flat heading in the axis order of DFRobot getCompassDegree(). Double atan2 on purpose: already linked.
    float heading = atan2(double(magX), double(magY)) * RAD_TO_DEG;
    if (heading < 0.0f)
        heading += 360.0f;
    // No tilt compensation, so a face-down or mirrored mount reverses the turn; only a mirror undoes that
    if (config.display.compass_orientation > meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_270)
        heading = 360.0f - heading;

    heading = applyCompassOrientation(heading);
    if (screen)
        screen->setHeading(heading);
#endif
    return BMM150_POLL_INTERVAL_MS;
}

void BMM150Sensor::calibrate(uint16_t forSeconds)
{
#if !defined(MESHTASTIC_EXCLUDE_SCREEN) && HAS_SCREEN
    LOG_DEBUG("BMM150 calibration started for %is", forSeconds);
    seedPending = true;
    startCalibrationWindow(forSeconds);
#endif
}

// ----------------------------------------------------------------------
// BMM150Singleton
// ----------------------------------------------------------------------

// Get a singleton wrapper for an Sparkfun BMM_150_I2C
BMM150Singleton *BMM150Singleton::GetInstance(ScanI2C::FoundDevice device)
{
    // resolved via the scanner: WIRE1 may be a bridged bus rather than the
    // local Wire1 (e.g. SenseCAP Indicator)
    TwoWire &bus = *ScanI2CTwoWire::fetchI2CBus(device.address);
    if (pinstance == nullptr) {
        pinstance = new BMM150Singleton(&bus, device.address.address);
    }
    return pinstance;
}

BMM150Singleton::~BMM150Singleton() {}

BMM150Singleton *BMM150Singleton::pinstance{nullptr};

// Initialise the BMM150 Sensor
// https://github.com/DFRobot/DFRobot_BMM150/blob/master/examples/getGeomagneticData/getGeomagneticData.ino
bool BMM150Singleton::init(ScanI2C::FoundDevice device)
{

    // startup
    LOG_DEBUG("BMM150 begin on addr 0x%02X (port=%d)", device.address.address, device.address.port);
    uint8_t status = begin();
    if (status != 0) {
        LOG_DEBUG("BMM150 init error %u", status);
        return false;
    }

    // Continuous 10 Hz. Not REGULAR: the library writes that preset's XY repetition count to Z
    setOperationMode(BMM150_POWERMODE_NORMAL);
    setPresetMode(BMM150_PRESETMODE_ENHANCED);
    setRate(BMM150_DATA_RATE_10HZ);
    setMeasurementXYZ();
    return true;
}

#endif
