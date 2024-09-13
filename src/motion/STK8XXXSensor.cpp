#include "STK8XXXSensor.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR 

STK8XXXSensor::STK8XXXSensor(ScanI2C::DeviceAddress address)
    : MotionSensor::MotionSensor(ScanI2C::DeviceType::STK8BAXX, address)
{
}

#ifdef STK8XXX_INT

volatile static bool STK_IRQ;

bool STK8XXXSensor::init()
{
    if (sensor.STK8xxx_Initialization(STK8xxx_VAL_RANGE_2G))
    {
        STK_IRQ = false;
        sensor.STK8xxx_Anymotion_init();
        pinMode(STK8XXX_INT, INPUT_PULLUP);
        attachInterrupt(
            digitalPinToInterrupt(STK8XXX_INT), []
            { STK_IRQ = true; }, RISING);

        LOG_DEBUG("STK8XXXSensor::init ok\n");
        return true;
    }
    LOG_DEBUG("STK8XXXSensor::init failed\n");
    return false;
}

int32_t STK8XXXSensor::runOnce()
{
    if (STK_IRQ)
    {
        STK_IRQ = false;
        if (config.display.wake_on_tap_or_motion)
        {
            wakeScreen();
        }
    }
    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#endif

#endif