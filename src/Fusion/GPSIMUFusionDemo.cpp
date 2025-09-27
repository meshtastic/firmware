#include "GPSIMUFusionDemo.h"
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_GPS

#include "GPSIMUFusion.h"
#include "motion/SensorLiveData.h"
#include <Arduino.h>

// Forward declarations for easy access to debug functions
void debugGPSIMUFusionNow();
void quickGPSIMUFusionStatus();

/**
 * @brief Simple demo/test function to show GPS+IMU fusion data
 * This function can be called periodically to log fusion data
 * Note: The fusion system now has automatic logging built-in
 */
void demonstrateGPSIMUFusion() {
    const GPSIMUFusionData* fusion = getGPSIMUFusionData();
    
    if (fusion == nullptr) {
        LOG_INFO("GPS+IMU Fusion: No data available");
        return;
    }
    
    if (!fusion->initialized) {
        LOG_INFO("GPS+IMU Fusion: Not initialized");
        return;
    }
    
    static uint32_t lastLogTime = 0;
    uint32_t now = millis();
    
    // Log fusion data every 10 seconds (the fusion system itself logs every 1-5 seconds)
    if (now - lastLogTime > 10000) {
        lastLogTime = now;
        
        LOG_INFO("=== GPS+IMU Fusion Demo Output ===");
        LOG_INFO("Valid: GPS=%s IMU=%s", 
                fusion->gps_valid ? "YES" : "NO",
                fusion->imu_valid ? "YES" : "NO");
        
        if (fusion->gps_valid || fusion->imu_valid) {
            LOG_INFO("Position: %.6f, %.6f, %.1fm", 
                    fusion->latitude, fusion->longitude, fusion->altitude);
            
            LOG_INFO("Velocity: N=%.2f E=%.2f D=%.2f (%.2f m/s)", 
                    fusion->velocity_north, fusion->velocity_east, 
                    fusion->velocity_down, fusion->speed);
            
            LOG_INFO("Orientation (Madgwick): R=%.1f P=%.1f Y=%.1f deg", 
                    fusion->roll, fusion->pitch, fusion->yaw);
            
            LOG_INFO("Quality: HDOP=%.1f Sats=%d HeadAcc=%.1f deg", 
                    fusion->hdop, fusion->satellites, fusion->heading_accuracy);
        }
        
        LOG_INFO("Last Update: GPS=%u IMU=%u ms ago", 
                now - fusion->last_gps_ms, now - fusion->last_imu_ms);
    }
}

/**
 * @brief Force immediate detailed debug output
 * Call this function manually to get detailed fusion debug info right now
 */
void debugGPSIMUFusionNow() {
    if (g_gps_imu_fusion.isValid()) {
        LOG_INFO("=== MANUAL FUSION DEBUG REQUEST ===");
        g_gps_imu_fusion.logFusionDataDetailed();
    } else {
        LOG_INFO("GPS+IMU Fusion: System not available or not valid");
    }
}

/**
 * @brief Force immediate quick debug output
 * Call this function for a quick status check
 */
void quickGPSIMUFusionStatus() {
    if (g_gps_imu_fusion.isValid()) {
        g_gps_imu_fusion.logFusionDataQuick();
    } else {
        LOG_INFO("FUSION: System offline");
    }
}

/**
 * @brief Example of how to use GPS+IMU fusion data in your application
 */
void exampleFusionUsage() {
    const GPSIMUFusionData* fusion = getGPSIMUFusionData();
    
    if (fusion && fusion->initialized) {
        // Check if we have valid navigation data
        if (fusion->gps_valid || fusion->imu_valid) {
            
            // Use high-accuracy position when GPS is available
            if (fusion->gps_valid && fusion->hdop < 5.0f) {
                // Use GPS position for navigation
                float lat = fusion->latitude;
                float lon = fusion->longitude;
                // ... use position for mapping, waypoint navigation, etc.
            }
            
            // Always use IMU orientation if available (higher rate, better for motion)
            if (fusion->imu_valid) {
                float heading = fusion->yaw;
                float pitch = fusion->pitch;
                float roll = fusion->roll;
                // ... use for compass display, attitude indicators, etc.
            }
            
            // Use velocity for motion detection and navigation
            if (fusion->speed > 0.5f) { // Moving
                float course = atan2(fusion->velocity_east, fusion->velocity_north) * 180.0f / M_PI;
                if (course < 0) course += 360.0f;
                // ... use course for navigation
            }
            
            // Example: Detect if device is being moved
            bool deviceMoving = (fusion->speed > 0.3f);
            
            // Example: Check GPS quality for different use cases
            if (fusion->gps_valid) {
                if (fusion->hdop < 2.0f && fusion->satellites >= 6) {
                    // High accuracy - suitable for precise navigation
                } else if (fusion->hdop < 5.0f) {
                    // Medium accuracy - suitable for general navigation
                } else {
                    // Low accuracy - use with caution
                }
            }
        }
    }
}

#endif // !MESHTASTIC_EXCLUDE_GPS