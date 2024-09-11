#pragma once
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "PowerFSM.h"
#include "concurrency/OSThread.h"
#include "main.h"
#include "power.h"

#include <Adafruit_LIS3DH.h>
#include <Adafruit_LSM6DS3TRC.h>
#include <Adafruit_MPU6050.h>
#ifdef STK8XXX_INT
#include <stk8baxx.h>
#endif
#include <Arduino.h>
#include <SensorBMA423.hpp>
#include <Wire.h>
#ifdef RAK_4631
#include "Fusion/Fusion.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Rak_BMX160.h>
#endif

#define ACCELEROMETER_CHECK_INTERVAL_MS 100
#define ACCELEROMETER_CLICK_THRESHOLD 40

volatile static bool STK_IRQ;

static inline int readRegister(uint8_t address, uint8_t reg, uint8_t *data, uint8_t len)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom((uint8_t)address, (uint8_t)len);
    uint8_t i = 0;
    while (Wire.available()) {
        data[i++] = Wire.read();
    }
    return 0; // Pass
}

static inline int writeRegister(uint8_t address, uint8_t reg, uint8_t *data, uint8_t len)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(data, len);
    return (0 != Wire.endTransmission());
}

class AccelerometerThread : public concurrency::OSThread
{
  public:
    explicit AccelerometerThread(ScanI2C::DeviceType type) : OSThread("AccelerometerThread")
    {
        if (accelerometer_found.port == ScanI2C::I2CPort::NO_I2C) {
            LOG_DEBUG("AccelerometerThread disabling due to no sensors found\n");
            disable();
            return;
        }
        acceleremoter_type = type;
#ifndef RAK_4631
        if (!config.display.wake_on_tap_or_motion && !config.device.double_tap_as_button_press) {
            LOG_DEBUG("AccelerometerThread disabling due to no interested configurations\n");
            disable();
            return;
        }
#endif
        init();
    }

    void start()
    {
        init();
        setIntervalFromNow(0);
    };

  protected:
    int32_t runOnce() override
    {
        canSleep = true; // Assume we should not keep the board awake

        if (acceleremoter_type == ScanI2C::DeviceType::MPU6050 && mpu.getMotionInterruptStatus()) {
            wakeScreen();
        } else if (acceleremoter_type == ScanI2C::DeviceType::STK8BAXX && STK_IRQ) {
            STK_IRQ = false;
            if (config.display.wake_on_tap_or_motion) {
                wakeScreen();
            }
        } else if (acceleremoter_type == ScanI2C::DeviceType::LIS3DH && lis.getClick() > 0) {
            uint8_t click = lis.getClick();
            if (!config.device.double_tap_as_button_press) {
                wakeScreen();
            }

            if (config.device.double_tap_as_button_press && (click & 0x20)) {
                buttonPress();
                return 500;
            }
        } else if (acceleremoter_type == ScanI2C::DeviceType::BMA423 && bmaSensor.readIrqStatus() != DEV_WIRE_NONE) {
            if (bmaSensor.isTilt() || bmaSensor.isDoubleTap()) {
                wakeScreen();
                return 500;
            }
#ifdef RAK_4631
        } else if (acceleremoter_type == ScanI2C::DeviceType::BMX160) {
            sBmx160SensorData_t magAccel;
            sBmx160SensorData_t gAccel;

            /* Get a new sensor event */
            bmx160.getAllData(&magAccel, NULL, &gAccel);

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

#endif
        } else if (acceleremoter_type == ScanI2C::DeviceType::LSM6DS3 && lsm.shake()) {
            wakeScreen();
            return 500;
        }

        return ACCELEROMETER_CHECK_INTERVAL_MS;
    }

  private:
    void init()
    {
        LOG_DEBUG("AccelerometerThread initializing\n");

        if (acceleremoter_type == ScanI2C::DeviceType::MPU6050 && mpu.begin(accelerometer_found.address)) {
            LOG_DEBUG("MPU6050 initializing\n");
            // setup motion detection
            mpu.setHighPassFilter(MPU6050_HIGHPASS_0_63_HZ);
            mpu.setMotionDetectionThreshold(1);
            mpu.setMotionDetectionDuration(20);
            mpu.setInterruptPinLatch(true); // Keep it latched.  Will turn off when reinitialized.
            mpu.setInterruptPinPolarity(true);
#ifdef STK8XXX_INT
        } else if (acceleremoter_type == ScanI2C::DeviceType::STK8BAXX && stk8baxx.STK8xxx_Initialization(STK8xxx_VAL_RANGE_2G)) {
            STK_IRQ = false;
            LOG_DEBUG("STX8BAxx initialized\n");
            stk8baxx.STK8xxx_Anymotion_init();
            pinMode(STK8XXX_INT, INPUT_PULLUP);
            attachInterrupt(
                digitalPinToInterrupt(STK8XXX_INT), [] { STK_IRQ = true; }, RISING);
#endif
        } else if (acceleremoter_type == ScanI2C::DeviceType::LIS3DH && lis.begin(accelerometer_found.address)) {
            LOG_DEBUG("LIS3DH initializing\n");
            lis.setRange(LIS3DH_RANGE_2_G);
            // Adjust threshold, higher numbers are less sensitive
            lis.setClick(config.device.double_tap_as_button_press ? 2 : 1, ACCELEROMETER_CLICK_THRESHOLD);
        } else if (acceleremoter_type == ScanI2C::DeviceType::BMA423 &&
                   bmaSensor.begin(accelerometer_found.address, &readRegister, &writeRegister)) {
            LOG_DEBUG("BMA423 initializing\n");
            bmaSensor.configAccelerometer(bmaSensor.RANGE_2G, bmaSensor.ODR_100HZ, bmaSensor.BW_NORMAL_AVG4,
                                          bmaSensor.PERF_CONTINUOUS_MODE);
            bmaSensor.enableAccelerometer();
            bmaSensor.configInterrupt(BMA4_LEVEL_TRIGGER, BMA4_ACTIVE_HIGH, BMA4_PUSH_PULL, BMA4_OUTPUT_ENABLE,
                                      BMA4_INPUT_DISABLE);

#ifdef BMA423_INT
            pinMode(BMA4XX_INT, INPUT);
            attachInterrupt(
                BMA4XX_INT,
                [] {
                    // Set interrupt to set irq value to true
                    BMA_IRQ = true;
                },
                RISING); // Select the interrupt mode according to the actual circuit
#endif

#ifdef T_WATCH_S3
            // Need to raise the wrist function, need to set the correct axis
            bmaSensor.setReampAxes(bmaSensor.REMAP_TOP_LAYER_RIGHT_CORNER);
#else
            bmaSensor.setReampAxes(bmaSensor.REMAP_BOTTOM_LAYER_BOTTOM_LEFT_CORNER);
#endif
            // bmaSensor.enableFeature(bmaSensor.FEATURE_STEP_CNTR, true);
            bmaSensor.enableFeature(bmaSensor.FEATURE_TILT, true);
            bmaSensor.enableFeature(bmaSensor.FEATURE_WAKEUP, true);
            // bmaSensor.resetPedometer();

            // Turn on feature interrupt
            bmaSensor.enablePedometerIRQ();
            bmaSensor.enableTiltIRQ();
            // It corresponds to isDoubleClick interrupt
            bmaSensor.enableWakeupIRQ();
#ifdef RAK_4631
        } else if (acceleremoter_type == ScanI2C::DeviceType::BMX160 && bmx160.begin()) {
            bmx160.ODR_Config(BMX160_ACCEL_ODR_100HZ, BMX160_GYRO_ODR_100HZ); // set output data rate

#endif
        } else if (acceleremoter_type == ScanI2C::DeviceType::LSM6DS3 && lsm.begin_I2C(accelerometer_found.address)) {
            LOG_DEBUG("LSM6DS3 initializing\n");
            // Default threshold of 2G, less sensitive options are 4, 8 or 16G
            lsm.setAccelRange(LSM6DS_ACCEL_RANGE_2_G);
#ifndef LSM6DS3_WAKE_THRESH
#define LSM6DS3_WAKE_THRESH 20
#endif
            lsm.enableWakeup(config.display.wake_on_tap_or_motion, 1, LSM6DS3_WAKE_THRESH);
            // Duration is number of occurances needed to trigger, higher threshold is less sensitive
        }
    }
    void wakeScreen()
    {
        if (powerFSM.getState() == &stateDARK) {
            LOG_INFO("Tap or motion detected. Turning on screen\n");
            powerFSM.trigger(EVENT_INPUT);
        }
    }

    void buttonPress()
    {
        LOG_DEBUG("Double-tap detected. Firing button press\n");
        powerFSM.trigger(EVENT_PRESS);
    }

    ScanI2C::DeviceType acceleremoter_type;
    Adafruit_MPU6050 mpu;
    Adafruit_LIS3DH lis;
#ifdef STK8XXX_INT
    STK8xxx stk8baxx;
#endif
    Adafruit_LSM6DS3TRC lsm;
    SensorBMA423 bmaSensor;
    bool BMA_IRQ = false;
#ifdef RAK_4631
    bool showingScreen = false;
    RAK_BMX160 bmx160;
    float highestX = 0, lowestX = 0, highestY = 0, lowestY = 0, highestZ = 0, lowestZ = 0;

    static void drawFrameCalibration(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
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
};

#endif