#include "ICM20948Sensor.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#ifdef ICM_20948_INT_PIN

// Flag when an interrupt has been detected
volatile static bool ICM20948_IRQ = false;

// Interrupt service routine
void ICM20948SetInterrupt()
{
    ICM20948_IRQ = true;
}

#endif

ICM20948Sensor::ICM20948Sensor(ScanI2C::DeviceAddress address)
    : MotionSensor::MotionSensor(ScanI2C::DeviceType::ICM20948, address)
{
}

// Initialise the ICM20948 Sensor
bool ICM20948Sensor::initSensor()
{
#ifdef ICM_20948_DEBUG
    // Set ICM_20948_DEBUG to enable helpful debug messages on Serial
    sensor.enableDebugging();
#endif

    // Initialize the ICM-20948
    sensor.begin(devicePort() == ScanI2C::I2CPort::WIRE1 ? Wire1 : Wire, deviceAddress() == ICM20948_ADDR ? 1 : 0);
    if (sensor.status != ICM_20948_Stat_Ok) {
        LOG_DEBUG("ICM20948Sensor::init begin - %s\n", sensor.statusString());
        return false;
    }

    // SW reset to make sure the device starts in a known state
    sensor.swReset();
    if (sensor.status != ICM_20948_Stat_Ok) {
        LOG_DEBUG("ICM20948Sensor::init reset - %s\n", sensor.statusString());
        return false;
    }
    delay(100);

    // Now wake the sensor up
    sensor.sleep(false);
    sensor.lowPower(false);

#ifdef ICM_20948_INT_PIN

    // Active low
    sensor.cfgIntActiveLow(true);
    LOG_DEBUG("ICM20948Sensor::init set cfgIntActiveLow - %s\n", sensor.statusString());

    // Push-pull
    sensor.cfgIntOpenDrain(false);
    LOG_DEBUG("ICM20948Sensor::init set cfgIntOpenDrain - %s\n", sensor.statusString());

    // If enabled, *ANY* read will clear the INT_STATUS register.
    sensor.cfgIntAnyReadToClear(true);
    LOG_DEBUG("ICM20948Sensor::init set cfgIntAnyReadToClear - %s\n", sensor.statusString());

    // Latch the interrupt until cleared
    sensor.cfgIntLatch(true);
    LOG_DEBUG("ICM20948Sensor::init set cfgIntLatch - %s\n", sensor.statusString());

    // Set up an interrupt pin with an internal pullup for active low
    pinMode(ICM_20948_INT_PIN, INPUT_PULLUP);

    // Set up an interrupt service routine
    attachInterrupt(ICM_20948_INT_PIN, ICM20948SetInterrupt, FALLING);

#endif

    return true;
}

#ifdef ICM_20948_DMP_IS_ENABLED

bool ICM20948Sensor::init()
{
    return false;
}

int32_t ICM20948Sensor::runOnce()
{
    return 0;
};

#else

bool ICM20948Sensor::init()
{
    // Initialise the sensor for simple Wake on Motion only
    if (!initSensor())
        return false;

    // Set WoM threshold in milli G's
    sensor.WOMThreshold(ICM_20948_WOM_THRESHOLD);
    LOG_DEBUG("ICM20948Sensor::init set WOMThreshold - %s\n", sensor.statusString());

    // Enable WoM Logic mode 1 = Compare the current sample with the previous sample
    sensor.WOMLogic(true, 1);
    LOG_DEBUG("ICM20948Sensor::init set WOMLogic - %s\n", sensor.statusString());

    // Enable interrupts on WakeOnMotion
    sensor.intEnableWOM(true);
    LOG_DEBUG("ICM20948Sensor::init set intEnableWOM - %s\n", sensor.statusString());

    return true;
}

#ifdef ICM_20948_INT_PIN

int32_t ICM20948Sensor::runOnce()
{
    // Wake on motion using hardware interrupts - this is the most efficient way to check for motion
    if (ICM20948_IRQ) {
        ICM20948_IRQ = false;
        sensor.clearInterrupts();
        wakeScreen();
    }
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#else

int32_t ICM20948Sensor::runOnce()
{
    // Wake on motion using polling  - this is not as efficient as using hardware interrupt pin (see above)
    auto status = sensor.setBank(0);
    if (sensor.status != ICM_20948_Stat_Ok) {
        LOG_DEBUG("ICM20948Sensor::isWakeOnMotion failed - %s\n", sensor.statusString());
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    }

    ICM_20948_INT_STATUS_t int_stat;
    status = sensor.read(AGB0_REG_INT_STATUS, (uint8_t *)&int_stat, sizeof(ICM_20948_INT_STATUS_t));
    if (status != ICM_20948_Stat_Ok) {
        LOG_DEBUG("ICM20948Sensor::isWakeOnMotion failed - %s\n", sensor.statusString());
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    }

    if (int_stat.WOM_INT != 0) {
        // Wake up!
        wakeScreen();
    }
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#endif

#endif

#endif