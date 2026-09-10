#include "BHI260APSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && defined(HAS_BHI260AP) && __has_include(<SensorBHI260AP.hpp>)
#define BOSCH_BHI260_KLIO

#include <BoschFirmware.h>
BHI260APSensor::BHI260APSensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}
// https://github.com/lewisxhe/SensorLib/blob/master/examples/Sensors/IMU/BHI260AP_InterruptSettings/BHI260AP_InterruptSettings.ino

bool BHI260APSensor::init()
{
    LOG_WARN("Initializing BHI260AP sensor %u", deviceAddress());
    sensor.setFirmware(bosch_firmware_image, bosch_firmware_size, bosch_firmware_type);
    sensor.setBootFromFlash(bosch_firmware_type);
    if (sensor.begin(Wire, deviceAddress())) {
        sensor.setRemapAxes(SensorRemap::TOP_LAYER_BOTTOM_RIGHT_CORNER);
        BoschSensorInfo info = sensor.getSensorInfo();

        LOG_INFO("Product ID     : %02x\n", info.getProductId());
        LOG_INFO("Kernel version : %04u\n", info.getKernelVersion());
        LOG_INFO("User version   : %04u\n", info.getUserVersion());
        LOG_INFO("ROM version    : %04u\n", info.getRomVersion());
        LOG_INFO("Power state    : %s\n", (info.getHostStatus() & BHY2_HST_POWER_STATE) ? "sleeping" : "active");
        LOG_INFO("Host interface : %s\n", (info.getHostStatus() & BHY2_HST_HOST_PROTOCOL) ? "SPI" : "I2C");
        LOG_INFO("Feature status : 0x%02x\n", info.getFeatStatus());

        stepCounter = new SensorStepCounter(sensor);
        // stepDetector = new SensorStepDetector(sensor);

        // sensor.configAccelerometer(sensor.RANGE_2G, sensor.ODR_100HZ, sensor.BW_NORMAL_AVG4, sensor.PERF_CONTINUOUS_MODE);
        // sensor.enableAccelerometer();
        // sensor.configInterrupt();

#ifdef BHI260AP_INT
        pinMode(BHI260AP_INT, INPUT);
        attachInterrupt(
            BHI260AP_INT,
            [] {
                // Set interrupt to set irq value to true
            },
            RISING); // Select the interrupt mode according to the actual circuit
#endif

        // stepDetector->enable(1.0, 0);
        stepCounter->enable(1.0, 0);
        LOG_DEBUG("BHI260AP init ok");
        return true;
    }
    LOG_DEBUG("BHI260AP init failed");
    return false;
}

int32_t BHI260APSensor::runOnce()
{
    sensor.update();
    if (stepCounter->hasUpdated()) {
        steps = stepCounter->getStepCount();
        LOG_WARN("Step count updated: %u", steps);
        if (screen)
            screen->steps = steps;
    }
    // LOG_WARN("Step count: %u", stepCounter->getStepCount());
    // if (sensor.readIrqStatus()) {
    //    if (sensor.isTilt() || sensor.isDoubleTap()) {
    //        wakeScreen();
    //        return 500;
    //    }
    //}
    return 1000;
}

#endif