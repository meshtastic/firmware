#include "QMI8658Sensor.h"
#include "NodeDB.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && __has_include(<SensorQMI8658.hpp>)

#include <math.h>

QMI8658Sensor::QMI8658Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice) {}

bool QMI8658Sensor::init()
{
    LOG_DEBUG("QMI8658: init start (SPI)");

    bool ok = false;
#if defined(ARCH_ESP32)
    // Use the secondary SPI host (HSPI) shared with SD/IMU on ESP32-S3.
    // Create a local static instance so we don't depend on a global SPI1 symbol.
    static SPIClass imuSPI(HSPI);
    auto &spiBus = imuSPI;
#else
    auto &spiBus = SPI;
#endif
#if defined(ARCH_ESP32)
    // Ensure HSPI is initialised with correct pins for this board
    LOG_DEBUG("QMI8658: SPI(HSPI).begin(sck=%d, miso=%d, mosi=%d, cs=%d)", (int)SPI_SCK, (int)SPI_MISO, (int)SPI_MOSI,
              (int)IMU_CS);
    spiBus.begin(SPI_SCK, SPI_MISO, SPI_MOSI, -1);
    pinMode(IMU_CS, OUTPUT);
    digitalWrite(IMU_CS, HIGH);
#endif

#if defined(SPI_MOSI) && defined(SPI_MISO) && defined(SPI_SCK)
    LOG_DEBUG("QMI8658: qmi.begin(bus=HSPI, cs=%d, mosi=%d, miso=%d, sck=%d)", (int)IMU_CS, (int)SPI_MOSI, (int)SPI_MISO,
              (int)SPI_SCK);
    ok = qmi.begin(spiBus, IMU_CS, SPI_MOSI, SPI_MISO, SPI_SCK);
#else
    LOG_DEBUG("QMI8658: qmi.begin(bus=?, cs=%d) default pins", (int)IMU_CS);
    ok = qmi.begin(spiBus, IMU_CS);
#endif

    if (!ok) {
        LOG_DEBUG("QMI8658: init failed (qmi.begin)");
        return false;
    }

    uint8_t id = qmi.getChipID();
    LOG_DEBUG("QMI8658: chip id=0x%02x", id);
#ifdef QMI8658_DEBUG_STREAM
    LOG_INFO("QMI8658 debug stream enabled (10 Hz)");
#endif

    // Basic configuration similar to lewisxhe examples
    qmi.configAccelerometer(
        SensorQMI8658::ACC_RANGE_4G,              // sensitivity
        SensorQMI8658::ACC_ODR_1000Hz,            // ODR
        SensorQMI8658::LPF_MODE_0                 // low-pass
    );

    qmi.configGyroscope(
        SensorQMI8658::GYR_RANGE_64DPS,           // range
        SensorQMI8658::GYR_ODR_896_8Hz,           // ODR
        SensorQMI8658::LPF_MODE_3                 // low-pass
    );

    LOG_DEBUG("QMI8658: enabling sensors (gyro+accel)");
    qmi.enableGyroscope();
    qmi.enableAccelerometer();

#ifdef IMU_INT
    if (config.display.wake_on_tap_or_motion) {
        LOG_DEBUG("QMI8658: enable INT1, disable INT2");
        qmi.enableINT(SensorQMI8658::INTERRUPT_PIN_1, true);
        qmi.enableINT(SensorQMI8658::INTERRUPT_PIN_2, false);
        LOG_DEBUG("QMI8658: INT enabled on IMU_INT=%d", IMU_INT);
    }
#endif

    LOG_DEBUG("QMI8658: dump control registers ->");
    qmi.dumpCtrlRegister();
    LOG_DEBUG("QMI8658: init ok");
    return true;
}

int32_t QMI8658Sensor::runOnce()
{
#ifdef QMI8658_DEBUG_STREAM
    // Always sample/log when debug stream is enabled
    IMUdata acc = {0};
    IMUdata gyr = {0};
    bool ready = qmi.getDataReady();
    bool gotAcc = qmi.getAccelerometer(acc.x, acc.y, acc.z);
    bool gotGyr = qmi.getGyroscope(gyr.x, gyr.y, gyr.z);
    LOG_DEBUG("QMI8658: ready=%d ACC[x=%.3f y=%.3f z=%.3f] m/s^2  GYR[x=%.3f y=%.3f z=%.3f] dps",
              (int)ready, acc.x, acc.y, acc.z, gyr.x, gyr.y, gyr.z);
    return 100; // ~10 Hz
#endif

    if (!config.display.wake_on_tap_or_motion)
        return MOTION_SENSOR_CHECK_INTERVAL_MS;

    if (qmi.getDataReady()) {
        IMUdata acc = {0};
        if (qmi.getAccelerometer(acc.x, acc.y, acc.z)) {
            // Convert to units of g (library returns m/s^2)
            const float g = 9.80665f;
            float magG = sqrtf((acc.x * acc.x + acc.y * acc.y + acc.z * acc.z)) / g;
            float delta = fabsf(magG - 1.0f);
            LOG_DEBUG("QMI8658: |a|=%.2fg delta=%.2fg", magG, delta);
            if (delta > MOTION_THRESHOLD_G) {
                wakeScreen();
                return 500; // pause a little after waking screen
            }
        }
    }

    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#endif
