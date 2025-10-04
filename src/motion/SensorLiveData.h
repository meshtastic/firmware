#pragma once

#include <stdint.h>

// Forward declaration for GPS+IMU fusion
struct GPSIMUFusionData;

struct Vec3f {
    float x = 0;
    float y = 0;
    float z = 0;
};

struct QMI8658LiveData {
    bool initialized = false;
    bool ready = false;
    Vec3f acc;  // m/s^2
    Vec3f gyr;  // dps
    // Fused orientation (degrees), using Fusion AHRS with QMC6310 magnetometer when available
    float roll = 0.0f;
    float pitch = 0.0f;
    float yaw = 0.0f;
    uint32_t last_ms = 0;
};

struct QMC6310LiveData {
    bool initialized = false;
    int16_t rawX = 0, rawY = 0, rawZ = 0;
    float offX = 0, offY = 0, offZ = 0;
    float heading = 0; // degrees 0..360
    // Scaled field strength in microtesla (after hard/soft iron corrections)
    float uT_X = 0, uT_Y = 0, uT_Z = 0;
    // Soft-iron scale factors applied to calibrated axes
    float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;
    uint32_t last_ms = 0;
};

extern QMI8658LiveData g_qmi8658Live;
extern QMC6310LiveData g_qmc6310Live;

// GPS+IMU fusion data access
#if !MESHTASTIC_EXCLUDE_GPS
extern class GPSIMUFusion g_gps_imu_fusion;
const GPSIMUFusionData* getGPSIMUFusionData();
#endif
