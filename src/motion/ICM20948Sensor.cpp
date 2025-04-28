#include "ICM20948Sensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<ICM_20948.h>)

// Flag when an interrupt has been detected
volatile static bool ICM20948_IRQ = false;

// Interrupt service routine
void ICM20948SetInterrupt()
{
    ICM20948_IRQ = true;
}

ICM20948Sensor::ICM20948Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

bool ICM20948Sensor::init()
{
    // Initialise the sensor
    sensor = ICM20948Singleton::GetInstance();
    if (!sensor->init(device))
        return false;

    // Enable simple Wake on Motion
    return sensor->setWakeOnMotion();
}

#ifdef ICM_20948_INT_PIN

int32_t ICM20948Sensor::runOnce()
{
    // Wake on motion using hardware interrupts - this is the most efficient way to check for motion
    if (ICM20948_IRQ) {
        ICM20948_IRQ = false;
        sensor->clearInterrupts();
        wakeScreen();
    }
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#else

int32_t ICM20948Sensor::runOnce()
{
    // Wake on motion using polling  - this is not as efficient as using hardware interrupt pin (see above)
    auto status = sensor->setBank(0);
    if (sensor->status != ICM_20948_Stat_Ok) {
        LOG_DEBUG("ICM20948 isWakeOnMotion failed to set bank - %s", sensor->statusString());
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    }

    ICM_20948_INT_STATUS_t int_stat;
    status = sensor->read(AGB0_REG_INT_STATUS, (uint8_t *)&int_stat, sizeof(ICM_20948_INT_STATUS_t));
    if (status != ICM_20948_Stat_Ok) {
        LOG_DEBUG("ICM20948 isWakeOnMotion failed to read interrupts - %s", sensor->statusString());
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    }

    if (int_stat.WOM_INT != 0) {
        // Wake up!
        wakeScreen();
    }
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#endif

// ----------------------------------------------------------------------
// ICM20948Singleton
// ----------------------------------------------------------------------

// Get a singleton wrapper for an Sparkfun ICM_20948_I2C
ICM20948Singleton *ICM20948Singleton::GetInstance()
{
    if (pinstance == nullptr) {
        pinstance = new ICM20948Singleton();
    }
    return pinstance;
}

ICM20948Singleton::ICM20948Singleton() {}

ICM20948Singleton::~ICM20948Singleton() {}

ICM20948Singleton *ICM20948Singleton::pinstance{nullptr};

// Initialise the ICM20948 Sensor
bool ICM20948Singleton::init(ScanI2C::FoundDevice device)
{
#ifdef ICM_20948_DEBUG
    // Set ICM_20948_DEBUG to enable helpful debug messages on Serial
    enableDebugging();
#endif

// startup
#ifdef Wire1
    ICM_20948_Status_e status =
        begin(device.address.port == ScanI2C::I2CPort::WIRE1 ? Wire1 : Wire, device.address.address == ICM20948_ADDR ? 1 : 0);
#else
    ICM_20948_Status_e status = begin(Wire, device.address.address == ICM20948_ADDR ? 1 : 0);
#endif
    if (status != ICM_20948_Stat_Ok) {
        LOG_DEBUG("ICM20948 init begin - %s", statusString());
        return false;
    }

    // SW reset to make sure the device starts in a known state
    if (swReset() != ICM_20948_Stat_Ok) {
        LOG_DEBUG("ICM20948 init reset - %s", statusString());
        return false;
    }
    delay(200);

    // Now wake the sensor up
    if (sleep(false) != ICM_20948_Stat_Ok) {
        LOG_DEBUG("ICM20948 init wake - %s", statusString());
        return false;
    }

    if (lowPower(false) != ICM_20948_Stat_Ok) {
        LOG_DEBUG("ICM20948 init high power - %s", statusString());
        return false;
    }

#ifdef ICM_20948_INT_PIN

    // Active low
    cfgIntActiveLow(true);
    LOG_DEBUG("ICM20948 init set cfgIntActiveLow - %s", statusString());

    // Push-pull
    cfgIntOpenDrain(false);
    LOG_DEBUG("ICM20948 init set cfgIntOpenDrain - %s", statusString());

    // If enabled, *ANY* read will clear the INT_STATUS register.
    cfgIntAnyReadToClear(true);
    LOG_DEBUG("ICM20948 init set cfgIntAnyReadToClear - %s", statusString());

    // Latch the interrupt until cleared
    cfgIntLatch(true);
    LOG_DEBUG("ICM20948 init set cfgIntLatch - %s", statusString());

    // Set up an interrupt pin with an internal pullup for active low
    pinMode(ICM_20948_INT_PIN, INPUT_PULLUP);

    // Set up an interrupt service routine
    attachInterrupt(ICM_20948_INT_PIN, ICM20948SetInterrupt, FALLING);

#endif
    return true;
}

#ifdef ICM_20948_DMP_IS_ENABLED

// Stub
bool ICM20948Sensor::initDMP()
{
    return false;
}

#endif

bool ICM20948Singleton::setWakeOnMotion()
{
    // Set WoM threshold in milli G's
    auto status = WOMThreshold(ICM_20948_WOM_THRESHOLD);
    if (status != ICM_20948_Stat_Ok)
        return false;

    // Enable WoM Logic mode 1 = Compare the current sample with the previous sample
    status = WOMLogic(true, 1);
    LOG_DEBUG("ICM20948 init set WOMLogic - %s", statusString());
    if (status != ICM_20948_Stat_Ok)
        return false;

    // Enable interrupts on WakeOnMotion
    status = intEnableWOM(true);
    LOG_DEBUG("ICM20948 init set intEnableWOM - %s", statusString());
    return status == ICM_20948_Stat_Ok;

    // Clear any current interrupts
    ICM20948_IRQ = false;
    clearInterrupts();
    return true;
}

#endif