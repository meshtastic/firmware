#pragma once

#include "configuration.h"
#include "motion/SensorLiveData.h"
#include "Fusion/Fusion.h"
#include <stdint.h>

#if !MESHTASTIC_EXCLUDE_GPS

// Forward declarations
extern class GPS *gps;

/**
 * @brief GPS+IMU fusion data structure containing combined navigation solution
 */
struct GPSIMUFusionData {
    bool initialized = false;
    bool gps_valid = false;
    bool imu_valid = false;
    
    // Position (from GPS, with IMU-aided smoothing)
    double latitude = 0.0;      // degrees
    double longitude = 0.0;     // degrees
    float altitude = 0.0f;      // meters MSL
    
    // Velocity (GPS-derived with IMU correction)
    float velocity_north = 0.0f; // m/s
    float velocity_east = 0.0f;  // m/s
    float velocity_down = 0.0f;  // m/s
    float speed = 0.0f;          // m/s (horizontal)
    
    // Orientation (IMU-derived with GPS heading aid)
    float roll = 0.0f;          // degrees
    float pitch = 0.0f;         // degrees
    float yaw = 0.0f;           // degrees (0-360, true north)
    
    // Quality indicators
    float hdop = 99.0f;         // horizontal dilution of precision
    uint8_t satellites = 0;     // number of satellites
    float heading_accuracy = 180.0f; // estimated heading accuracy (degrees)
    
    // Timestamps
    uint32_t last_gps_ms = 0;   // last GPS update
    uint32_t last_imu_ms = 0;   // last IMU update
    uint32_t last_fusion_ms = 0; // last fusion update
};

/**
 * @brief GPS-aided IMU sensor fusion class
 * 
 * Combines GPS and IMU data to provide improved navigation solution:
 * - Uses IMU for high-rate orientation and short-term position tracking
 * - Uses GPS for absolute position reference and drift correction
 * - Uses GPS course when moving to aid IMU yaw estimation
 * - Provides smooth, accurate navigation data
 */
class GPSIMUFusion {
private:
    GPSIMUFusionData fusionData;
    
    // Fusion algorithm state
    FusionAhrs ahrs;
    bool ahrsInitialized = false;
    
    // GPS filtering state
    struct {
        double lat_filtered = 0.0;
        double lon_filtered = 0.0; 
        float alt_filtered = 0.0f;
        float course_filtered = 0.0f;
        float speed_filtered = 0.0f;
        uint32_t last_course_ms = 0;
        bool moving = false;
    } gps_state;
    
    // IMU integration state
    struct {
        FusionVector velocity = {0};
        FusionVector position = {0};
        uint32_t last_update_ms = 0;
        bool initialized = false;
    } imu_state;
    
    // Configuration
    static constexpr float GPS_VELOCITY_THRESHOLD = 1.0f; // m/s - minimum speed for GPS heading
    static constexpr float GPS_TIMEOUT_MS = 5000.0f;     // GPS data timeout
    static constexpr float IMU_TIMEOUT_MS = 1000.0f;     // IMU data timeout
    static constexpr float FUSION_UPDATE_RATE = 50.0f;   // Hz
    
    // Internal methods
    void initializeAHRS();
    void updateIMU(const QMI8658LiveData& imuData, const QMC6310LiveData& magData, float dt);
    void updateGPS();
    void fuseNavigationData(float dt);
    bool isGPSDataValid();
    bool isIMUDataValid();
    float normalizeAngle(float angle);
    void lowPassFilter(float& filtered, float new_value, float alpha);

public:
    GPSIMUFusion();
    
    /**
     * @brief Initialize the GPS+IMU fusion system
     * @return true if initialization successful
     */
    bool initialize();
    
    /**
     * @brief Update fusion with new sensor data
     * Should be called regularly (50-100 Hz recommended)
     * @return true if fusion data was updated
     */
    bool update();
    
    /**
     * @brief Get the current fused navigation data
     * @return reference to fusion data structure
     */
    const GPSIMUFusionData& getFusionData() const { return fusionData; }
    
    /**
     * @brief Check if fusion system is providing valid data
     * @return true if valid navigation solution available
     */
    bool isValid() const { return fusionData.initialized && (fusionData.gps_valid || fusionData.imu_valid); }
    
    /**
     * @brief Reset the fusion system
     * Call when major discontinuity detected (e.g., position jump)
     */
    void reset();
    
    /**
     * @brief Log detailed fusion data for debugging
     * Called automatically every 5 seconds when fusion is active
     */
    void logFusionDataDetailed();
    
    /**
     * @brief Log quick fusion status for monitoring
     * Called automatically every 1 second when fusion is active
     */
    void logFusionDataQuick();
};

// Global instance
extern GPSIMUFusion g_gps_imu_fusion;

#endif // !MESHTASTIC_EXCLUDE_GPS
