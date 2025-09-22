#pragma once

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_GPS

/**
 * @brief GPS+IMU Fusion Debug Interface
 * 
 * This header provides easy access to GPS+IMU fusion debugging functions.
 * Include this in any file where you want to access fusion debug data.
 * 
 * Usage:
 * 1. Include this header: #include "motion/GPSIMUFusionDebug.h"
 * 2. Call debug functions as needed
 * 
 * Auto Logging:
 * - The fusion system automatically logs data every 1-5 seconds when active
 * - No manual calls needed for normal operation
 * 
 * Manual Debug Functions:
 * - debugGPSIMUFusionNow() - Force detailed debug output immediately
 * - quickGPSIMUFusionStatus() - Quick status check
 * - demonstrateGPSIMUFusion() - Demo function with periodic output
 * - getGPSIMUFusionData() - Get fusion data structure directly
 */

// Manual debug functions
extern void debugGPSIMUFusionNow();
extern void quickGPSIMUFusionStatus();
extern void demonstrateGPSIMUFusion();

// Data access function
extern const struct GPSIMUFusionData* getGPSIMUFusionData();

// Direct access to fusion system
extern class GPSIMUFusion g_gps_imu_fusion;

/**
 * @brief Easy macro to add fusion debug to any file
 * 
 * Usage in your code:
 * DEBUG_FUSION_NOW();  // Immediate detailed debug
 * DEBUG_FUSION_QUICK(); // Quick status
 */
#define DEBUG_FUSION_NOW()   debugGPSIMUFusionNow()
#define DEBUG_FUSION_QUICK() quickGPSIMUFusionStatus()

/**
 * @brief Check if GPS+IMU fusion is available and working
 */
#define FUSION_IS_AVAILABLE() (g_gps_imu_fusion.isValid())

/**
 * @brief Get current fusion position if available
 * @param lat Pointer to store latitude (degrees)
 * @param lon Pointer to store longitude (degrees)
 * @return true if valid position available
 */
inline bool getFusionPosition(double* lat, double* lon) {
    const GPSIMUFusionData* fusion = getGPSIMUFusionData();
    if (fusion && (fusion->gps_valid || fusion->imu_valid)) {
        *lat = fusion->latitude;
        *lon = fusion->longitude;
        return true;
    }
    return false;
}

/**
 * @brief Get current fusion orientation if available
 * @param roll Pointer to store roll (degrees)
 * @param pitch Pointer to store pitch (degrees) 
 * @param yaw Pointer to store yaw/heading (degrees)
 * @return true if valid orientation available
 */
inline bool getFusionOrientation(float* roll, float* pitch, float* yaw) {
    const GPSIMUFusionData* fusion = getGPSIMUFusionData();
    if (fusion && fusion->imu_valid) {
        *roll = fusion->roll;
        *pitch = fusion->pitch;
        *yaw = fusion->yaw;
        return true;
    }
    return false;
}

/**
 * @brief Get current fusion speed if available
 * @return speed in m/s, or -1.0 if not available
 */
inline float getFusionSpeed() {
    const GPSIMUFusionData* fusion = getGPSIMUFusionData();
    if (fusion && (fusion->gps_valid || fusion->imu_valid)) {
        return fusion->speed;
    }
    return -1.0f;
}

#endif // !MESHTASTIC_EXCLUDE_GPS
