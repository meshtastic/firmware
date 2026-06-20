#include "QMA6100PSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && defined(HAS_QMA6100P)

// Flag when an interrupt has been detected
volatile static bool QMA6100P_IRQ = false;

// Interrupt service routine
void QMA6100PSetInterrupt()
{
    QMA6100P_IRQ = true;
}

QMA6100PSensor::QMA6100PSensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

bool QMA6100PSensor::init()
{
    // Initialise the sensor
    sensor = QMA6100PSingleton::GetInstance();
    if (!sensor->init(device))
        return false;

    // Enable simple Wake on Motion
    return sensor->setWakeOnMotion();
}

#ifdef QMA_6100P_INT_PIN

int32_t QMA6100PSensor::runOnce()
{
    // Wake on motion using hardware interrupts - this is the most efficient way to check for motion
    if (QMA6100P_IRQ) {
        QMA6100P_IRQ = false;
        wakeScreen();
    }
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#else

int32_t QMA6100PSensor::runOnce()
{
    // Wake on motion using polling  - this is not as efficient as using hardware interrupt pin (see above)

    uint8_t tempVal;
    if (!sensor->readRegisterRegion(SFE_QMA6100P_INT_ST0, &tempVal, 1)) {
        LOG_DEBUG("QMA6100PS isWakeOnMotion failed to read interrupts");
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    }

    if ((tempVal & 7) != 0) {
        // Wake up!
        wakeScreen();
    }
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#endif

// ----------------------------------------------------------------------
// QMA6100PSingleton
// ----------------------------------------------------------------------

// Get a singleton wrapper for an Sparkfun QMA_6100P_I2C
QMA6100PSingleton *QMA6100PSingleton::GetInstance()
{
    if (pinstance == nullptr) {
        pinstance = new QMA6100PSingleton();
    }
    return pinstance;
}

QMA6100PSingleton::QMA6100PSingleton() {}

QMA6100PSingleton::~QMA6100PSingleton() {}

QMA6100PSingleton *QMA6100PSingleton::pinstance{nullptr};

// Initialise the QMA6100P Sensor
bool QMA6100PSingleton::init(ScanI2C::FoundDevice device)
{

// startup
#ifdef Wire1
    bool status = begin(device.address.address, device.address.port == ScanI2C::I2CPort::WIRE1 ? &Wire1 : &Wire);
#else
    // check chip id
    bool status = begin(device.address.address, &Wire);
#endif
    if (status != true) {
        LOG_WARN("QMA6100P init begin failed");
        return false;
    }
    delay(20);
    // SW reset to make sure the device starts in a known state
    if (softwareReset() != true) {
        LOG_WARN("QMA6100P init reset failed");
        return false;
    }
    delay(20);
    // Set range
    if (!setRange(QMA_6100P_MPU_ACCEL_SCALE)) {
        LOG_WARN("QMA6100P init range failed");
        return false;
    }
    // set active mode
    if (!enableAccel()) {
        LOG_WARN("ERROR QMA6100P active mode set failed");
    }
    // set calibrateoffsets
    if (!calibrateOffsets()) {
        LOG_WARN("ERROR QMA6100P calibration failed");
    }
#ifdef QMA_6100P_INT_PIN

    // Active low & Open Drain
    uint8_t tempVal;
    if (!readRegisterRegion(SFE_QMA6100P_INTPINT_CONF, &tempVal, 1)) {
        LOG_WARN("QMA6100P init failed to read interrupt pin config");
        return false;
    }

    tempVal |= 0b00000010; // Active low & Open Drain

    if (!writeRegisterByte(SFE_QMA6100P_INTPINT_CONF, tempVal)) {
        LOG_WARN("QMA6100P init failed to write interrupt pin config");
        return false;
    }

    // Latch until cleared, all reads clear the latch
    if (!readRegisterRegion(SFE_QMA6100P_INT_CFG, &tempVal, 1)) {
        LOG_WARN("QMA6100P init failed to read interrupt config");
        return false;
    }

    tempVal |= 0b10000001; // Latch until cleared, INT_RD_CLR1

    if (!writeRegisterByte(SFE_QMA6100P_INT_CFG, tempVal)) {
        LOG_WARN("QMA6100P init failed to write interrupt config");
        return false;
    }
    // Set up an interrupt pin with an internal pullup for active low
    pinMode(QMA_6100P_INT_PIN, INPUT_PULLUP);

    // Set up an interrupt service routine
    attachInterrupt(QMA_6100P_INT_PIN, QMA6100PSetInterrupt, FALLING);

#endif
    return true;
}

bool QMA6100PSingleton::setWakeOnMotion()
{
    // Enable 'Any Motion' interrupt
    if (!writeRegisterByte(SFE_QMA6100P_INT_EN2, 0b00000111)) {
        LOG_WARN("QMA6100P :setWakeOnMotion failed to write interrupt enable");
        return false;
    }

    // Set 'Significant Motion' interrupt map to INT1
    uint8_t tempVal;

    if (!readRegisterRegion(SFE_QMA6100P_INT_MAP1, &tempVal, 1)) {
        LOG_WARN("QMA6100P setWakeOnMotion failed to read interrupt map");
        return false;
    }

    sfe_qma6100p_int_map1_bitfield_t int_map1;
    int_map1.all = tempVal;
    int_map1.bits.int1_any_mot = 1; // any motion interrupt to INT1
    tempVal = int_map1.all;

    if (!writeRegisterByte(SFE_QMA6100P_INT_MAP1, tempVal)) {
        LOG_WARN("QMA6100P setWakeOnMotion failed to write interrupt map");
        return false;
    }

    // Clear any current interrupts
    QMA6100P_IRQ = false;
    return true;
}

#endif
