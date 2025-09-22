#include "GPSIMUFusion.h"
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_GPS

#include "gps/GPS.h"
#include "SensorLiveData.h"
#include "Fusion/Fusion.h"
#include <math.h>
#include <Arduino.h>
#include <cmath> // For sqrt()

// Global instance
GPSIMUFusion g_gps_imu_fusion;

// External data references
extern QMI8658LiveData g_qmi8658Live;
extern QMC6310LiveData g_qmc6310Live;
extern GPS *gps;

GPSIMUFusion::GPSIMUFusion() {
    // Constructor - initialization done in initialize()
}

bool GPSIMUFusion::initialize() {
    if (fusionData.initialized) {
        return true; // Already initialized
    }
    
    // Initialize AHRS
    initializeAHRS();
    
    // Reset state
    reset();
    
    fusionData.initialized = true;
    LOG_INFO("GPS+IMU Fusion initialized");
    
    return true;
}

void GPSIMUFusion::initializeAHRS() {
    if (ahrsInitialized) {
        return;
    }
    
    FusionAhrsInitialise(&ahrs);
    
    FusionAhrsSettings settings;
    settings.convention = FusionConventionNed;      // North-East-Down frame
    settings.gain = 0.5f;                          // Fusion gain (lower = more GPS influence when available)
    settings.gyroscopeRange = 512.0f;              // degrees per second
    settings.accelerationRejection = 15.0f;        // degrees (higher for vehicle applications)
    settings.magneticRejection = 15.0f;            // degrees
    settings.recoveryTriggerPeriod = 5;            // cycles
    
    FusionAhrsSetSettings(&ahrs, &settings);
    ahrsInitialized = true;
}

bool GPSIMUFusion::update() {
    if (!fusionData.initialized) {
        return false;
    }
    
    uint32_t now_ms = millis();
    static uint32_t last_update_ms = 0;
    
    // Calculate time delta
    float dt = (last_update_ms == 0) ? (1.0f / FUSION_UPDATE_RATE) : (now_ms - last_update_ms) / 1000.0f;
    dt = constrain(dt, 0.001f, 0.1f); // Limit dt to reasonable range
    last_update_ms = now_ms;
    
    // Check data validity
    fusionData.gps_valid = isGPSDataValid();
    fusionData.imu_valid = isIMUDataValid();
    
    bool updated = false;
    
    // Update IMU-based navigation
    if (fusionData.imu_valid) {
        updateIMU(g_qmi8658Live, g_qmc6310Live, dt);
        fusionData.last_imu_ms = now_ms;
        updated = true;
    }
    
    // Update GPS data
    if (fusionData.gps_valid) {
        updateGPS();
        fusionData.last_gps_ms = now_ms;
        updated = true;
    }
    
    // Perform sensor fusion
    if (updated) {
        fuseNavigationData(dt);
        fusionData.last_fusion_ms = now_ms;
    }
    
    return updated;
}

void GPSIMUFusion::updateIMU(const QMI8658LiveData& imuData, const QMC6310LiveData& magData, float dt) {
    // Create Fusion vectors from IMU data
    FusionVector gyroscope = {.axis = {.x = imuData.gyr.x, .y = imuData.gyr.y, .z = imuData.gyr.z}};
    FusionVector accelerometer = {.axis = {.x = imuData.acc.x, .y = imuData.acc.y, .z = imuData.acc.z}};
    
    // Use magnetometer if available and recent
    uint32_t now_ms = millis();
    bool magValid = magData.initialized && (now_ms - magData.last_ms) <= 200;
    
    if (magValid) {
        FusionVector magnetometer = {.axis = {.x = magData.uT_X, .y = magData.uT_Y, .z = magData.uT_Z}};
        FusionAhrsUpdate(&ahrs, gyroscope, accelerometer, magnetometer, dt);
    } else {
        FusionAhrsUpdateNoMagnetometer(&ahrs, gyroscope, accelerometer, dt);
    }
    
    // Get orientation from AHRS
    FusionQuaternion quaternion = FusionAhrsGetQuaternion(&ahrs);
    FusionEuler euler = FusionQuaternionToEuler(quaternion);
    
    // Store IMU-derived orientation (these will be used as base for fusion)
    fusionData.roll = euler.angle.roll;
    fusionData.pitch = euler.angle.pitch;
    
    // Don't update yaw directly from IMU if GPS heading is available and vehicle is moving
    if (!gps_state.moving || (now_ms - gps_state.last_course_ms) > 2000) {
        fusionData.yaw = normalizeAngle(euler.angle.yaw);
    }
    
    // Simple IMU velocity integration (will be corrected by GPS)
    if (!imu_state.initialized) {
        imu_state.velocity = (FusionVector){.axis = {0, 0, 0}};
        imu_state.position = (FusionVector){.axis = {0, 0, 0}};
        imu_state.initialized = true;
    }
    
    // Transform accelerometer reading to world frame and integrate
    FusionMatrix rotationMatrix = FusionQuaternionToMatrix(quaternion);
    FusionVector worldAccel = FusionMatrixMultiplyVector(rotationMatrix, accelerometer);
    
    // Remove gravity (assuming Z is down in NED frame)
    worldAccel.axis.z += 9.80665f;
    
    // Simple velocity integration (this will drift, GPS will correct)
    imu_state.velocity.axis.x += worldAccel.axis.x * dt;
    imu_state.velocity.axis.y += worldAccel.axis.y * dt;
    imu_state.velocity.axis.z += worldAccel.axis.z * dt;
}

void GPSIMUFusion::updateGPS() {
    if (!gps || !gps->hasLock()) {
        return;
    }
    
    uint32_t now_ms = millis();
    
    // Get GPS position with higher precision
    double gps_lat = gps->p.latitude_i * 1e-7;
    double gps_lon = gps->p.longitude_i * 1e-7;
    float gps_alt = gps->p.altitude;
    
    // Validate GPS coordinates are reasonable
    if (abs(gps_lat) < 0.0001 && abs(gps_lon) < 0.0001) {
        LOG_DEBUG("GPS coordinates too close to 0,0 - likely invalid: %.7f, %.7f", gps_lat, gps_lon);
        return;
    }
    
    // Smart GPS filtering for improved accuracy
    // Adjust filter strength based on GPS quality
    float base_alpha = 0.4f; // Moderate filtering (was 0.8f - too light)
    float gps_alpha = base_alpha;
    
    // Get GPS quality indicators
    float hdop_m = gps->p.HDOP / 100.0f; // Convert cm to meters
    uint8_t sats = gps->p.sats_in_view;
    
    // Adjust filter based on GPS quality (tighter filtering for poor quality)
    if (hdop_m > 5.0f || sats < 4) {
        gps_alpha = 0.2f; // Heavier filtering for poor GPS
    } else if (hdop_m < 2.0f && sats >= 6) {
        gps_alpha = 0.6f; // Lighter filtering for good GPS
    }
    
    if (gps_state.lat_filtered == 0.0 && gps_state.lon_filtered == 0.0) {
        // First GPS fix - initialize directly without filtering
        gps_state.lat_filtered = gps_lat;
        gps_state.lon_filtered = gps_lon;
        gps_state.alt_filtered = gps_alt;
        LOG_INFO("GPS INIT: First fix set to lat=%.8f lon=%.8f alt=%.1f (hdop=%.1fm sats=%d)", 
                gps_lat, gps_lon, gps_alt, hdop_m, sats);
    } else {
        // Calculate distance from current filtered position to new GPS reading
        double lat_diff = gps_lat - gps_state.lat_filtered;
        double lon_diff = gps_lon - gps_state.lon_filtered;
        double distance_deg = sqrt(lat_diff*lat_diff + lon_diff*lon_diff);
        double distance_m = distance_deg * 111320.0; // Rough conversion to meters
        
        // Adaptive jump detection based on GPS quality
        float max_jump = (hdop_m > 10.0f) ? 200.0f : 50.0f; // Allow larger jumps for poor GPS
        
        // If GPS reading is very different, reset to new position (might be location jump)
        if (distance_m > max_jump) {
            LOG_INFO("GPS RESET: Large jump detected (%.1fm > %.1fm), resetting filter", distance_m, max_jump);
            gps_state.lat_filtered = gps_lat;
            gps_state.lon_filtered = gps_lon;
            gps_state.alt_filtered = gps_alt;
        } else {
            // Apply adaptive low-pass filter - convert double to float for filtering
            float lat_f = (float)gps_lat;
            float lon_f = (float)gps_lon;
            float lat_filtered_f = (float)gps_state.lat_filtered;
            float lon_filtered_f = (float)gps_state.lon_filtered;
            
            lowPassFilter(lat_filtered_f, lat_f, gps_alpha);
            lowPassFilter(lon_filtered_f, lon_f, gps_alpha);
            lowPassFilter(gps_state.alt_filtered, gps_alt, gps_alpha);
            
            // Convert back to double
            gps_state.lat_filtered = (double)lat_filtered_f;
            gps_state.lon_filtered = (double)lon_filtered_f;
            
            // Debug position filtering
            static uint32_t lastPosDebug = 0;
            if (now_ms - lastPosDebug > 5000) {
                lastPosDebug = now_ms;
                LOG_INFO("GPS FILTER: raw(%.8f,%.8f) -> filtered(%.8f,%.8f) diff=%.1fm alpha=%.2f", 
                        gps_lat, gps_lon, gps_state.lat_filtered, gps_state.lon_filtered, distance_m, gps_alpha);
            }
        }
    }
    
    // Get GPS velocity and course from the public position structure
    float gps_speed = 0.0f;
    float gps_course = 0.0f;
    
    // Extract speed and course from GPS position structure
    // ground_speed is in km/h, ground_track is in degrees * 10^-5
    // Note: these are uint32_t, so we check for has_* flags or reasonable values
    if (gps->p.has_ground_speed) { // Use protobuf flag for validity
        gps_speed = gps->p.ground_speed / 3.6f; // Convert km/h to m/s
    }
    if (gps->p.has_ground_track) { // Use protobuf flag for validity
        gps_course = gps->p.ground_track / 1e5f; // Convert from degrees * 10^-5 to degrees
        
        // Check if vehicle is moving
        gps_state.moving = (gps_speed > GPS_VELOCITY_THRESHOLD);
        
        if (gps_state.moving) {
            // Apply smoothing to course only when moving
            if (gps_state.last_course_ms == 0) {
                gps_state.course_filtered = gps_course;
            } else {
                // Handle angle wrapping for course filtering
                float course_diff = gps_course - gps_state.course_filtered;
                if (course_diff > 180.0f) course_diff -= 360.0f;
                if (course_diff < -180.0f) course_diff += 360.0f;
                gps_state.course_filtered += course_diff * 0.2f; // Light smoothing
                gps_state.course_filtered = normalizeAngle(gps_state.course_filtered);
            }
            gps_state.last_course_ms = now_ms;
        }
        
        // Apply smoothing to speed
        lowPassFilter(gps_state.speed_filtered, gps_speed, 0.4f);
    }
    
    // Store GPS quality information  
    fusionData.hdop = gps->p.HDOP / 100.0f; // Convert from cm to meters
    fusionData.satellites = gps->p.sats_in_view;
    
    // Estimate heading accuracy based on speed and HDOP
    if (gps_state.moving && gps_speed > 2.0f) {
        fusionData.heading_accuracy = constrain(5.0f / gps_speed + fusionData.hdop, 2.0f, 45.0f);
    } else {
        fusionData.heading_accuracy = 180.0f; // No reliable heading when stationary
    }
    
    // Debug GPS data processing with more detail
    static uint32_t lastGpsDataDebug = 0;
    if (now_ms - lastGpsDataDebug > 3000) {
        lastGpsDataDebug = now_ms;
        LOG_INFO("GPS CONVERSION: lat_i=%ld -> lat=%.8f", gps->p.latitude_i, gps_lat);
        LOG_INFO("GPS CONVERSION: lon_i=%ld -> lon=%.8f", gps->p.longitude_i, gps_lon);
        LOG_INFO("GPS SPEED: raw_kmh=%.2f -> speed_ms=%.3f (filtered=%.3f)", 
                gps->p.ground_speed, gps_speed, gps_state.speed_filtered);
        LOG_INFO("GPS COURSE: raw_1e5=%ld -> course_deg=%.2f (filtered=%.2f)", 
                gps->p.ground_track, gps_course, gps_state.course_filtered);
        LOG_INFO("GPS QUALITY: hdop=%dcm(%.2fm) sats=%d moving=%s", 
                gps->p.HDOP, fusionData.hdop, fusionData.satellites, gps_state.moving ? "YES" : "NO");
    }
}

void GPSIMUFusion::fuseNavigationData(float dt) {
    // Position fusion: GPS is the primary reference, IMU provides smoothing
    if (fusionData.gps_valid) {
        fusionData.latitude = gps_state.lat_filtered;
        fusionData.longitude = gps_state.lon_filtered;
        fusionData.altitude = gps_state.alt_filtered;
        
        // Reset IMU velocity integration periodically to prevent drift
        if (fusionData.imu_valid) {
            // Simple complementary filter for velocity
            float gps_weight = 0.1f; // GPS has lower weight due to lower update rate but higher accuracy
            
            // Convert GPS course and speed to velocity components (if moving)
            if (gps_state.moving) {
                float course_rad = gps_state.course_filtered * M_PI / 180.0f;
                float gps_vel_north = gps_state.speed_filtered * cos(course_rad);
                float gps_vel_east = gps_state.speed_filtered * sin(course_rad);
                
                // Blend GPS and IMU velocities
                fusionData.velocity_north = (1.0f - gps_weight) * imu_state.velocity.axis.x + gps_weight * gps_vel_north;
                fusionData.velocity_east = (1.0f - gps_weight) * imu_state.velocity.axis.y + gps_weight * gps_vel_east;
                
                // Correct IMU velocity integration
                imu_state.velocity.axis.x = fusionData.velocity_north;
                imu_state.velocity.axis.y = fusionData.velocity_east;
            }
        }
        
        fusionData.speed = gps_state.speed_filtered;
    }
    
    // Heading fusion: Use GPS course when moving, IMU yaw otherwise
    if (fusionData.gps_valid && gps_state.moving && fusionData.heading_accuracy < 20.0f) {
        // Vehicle is moving and GPS heading is reliable
        float heading_weight = constrain(1.0f / (fusionData.heading_accuracy / 10.0f), 0.1f, 0.8f);
        
        // Blend GPS course and IMU yaw
        float yaw_diff = gps_state.course_filtered - fusionData.yaw;
        if (yaw_diff > 180.0f) yaw_diff -= 360.0f;
        if (yaw_diff < -180.0f) yaw_diff += 360.0f;
        
        fusionData.yaw += yaw_diff * heading_weight;
        fusionData.yaw = normalizeAngle(fusionData.yaw);
    }
    
    // If only IMU is valid, use pure IMU data (this is handled in updateIMU)
    
    // Debug logging - periodic detailed output
    static uint32_t lastDetailedLog = 0;
    uint32_t now_ms = millis();
    
    if (now_ms - lastDetailedLog > 5000) { // Every 5 seconds
        lastDetailedLog = now_ms;
        logFusionDataDetailed();
    }
    
    // High-frequency fusion status log
    static uint32_t lastQuickLog = 0;
    if (now_ms - lastQuickLog > 1000) { // Every 1 second
        lastQuickLog = now_ms;
        logFusionDataQuick();
    }
}

bool GPSIMUFusion::isGPSDataValid() {
    if (!gps) return false;
    
    /* 
     * GPS VALIDATION LOGIC - T-Beam Supreme
     * 
     * CURRENT: Very lenient for testing pipeline
     * TODO: Once GPS data flows properly, tighten these for production:
     * 
     * For better accuracy (reduce 50m offset):
     * - hasMinSats >= 4 (not 1)  
     * - hasHDOP < 300cm (3m) for good fixes, < 500cm (5m) acceptable
     * - Re-enable hasLock requirement: dataValid = hasLock && hasPositionData && coordinatesReasonable
     * - Add recentData check back to timing validation
     * 
     * For T-Beam Supreme indoor/weak signal:
     * - Keep hasLock optional if coordinates look valid
     * - Use GPS accuracy field (if available) instead of raw HDOP
     * - Consider fix type (2D vs 3D) from GPS status
     */
    
    uint32_t now_ms = millis();
    bool hasLock = gps->hasLock();
    bool recentData = (now_ms - fusionData.last_gps_ms) < GPS_TIMEOUT_MS;
    
    // Check for actual position data (coordinates not zero and reasonable)
    bool hasPositionData = (gps->p.latitude_i != 0 || gps->p.longitude_i != 0);
    bool coordinatesReasonable = (abs(gps->p.latitude_i) <= 900000000 && abs(gps->p.longitude_i) <= 1800000000);
    
    // TEMPORARY: Very lenient validation for testing the pipeline
    // TODO: Tighten these checks once pipeline is confirmed working
    bool hasMinSats = (gps->p.sats_in_view >= 1); // PRODUCTION: Change to >= 4
    bool hasHDOP = (gps->p.HDOP >= 0); // PRODUCTION: Change to < 300-500cm
    
    // For T-Beam Supreme: Accept coordinates even without hasLock() for testing
    // hasLock() may be too strict for indoor/weak signal conditions  
    bool dataValid = hasPositionData && coordinatesReasonable; // PRODUCTION: Add hasLock back
    bool qualityOk = hasMinSats || hasHDOP || (fusionData.last_gps_ms == 0); // Very lenient
    
    // Debug GPS validation issues - more detailed
    static uint32_t lastGpsDebug = 0;
    if (now_ms - lastGpsDebug > 1000) { // Every second for debugging
        lastGpsDebug = now_ms;
        LOG_INFO("GPS DEBUG: lock=%s pos=%s coords=%s sats=%s hdop=%s recent=%s", 
                hasLock ? "OK" : "FAIL", 
                hasPositionData ? "OK" : "FAIL",
                coordinatesReasonable ? "OK" : "FAIL",
                hasMinSats ? "OK" : "FAIL",
                hasHDOP ? "OK" : "FAIL",
                recentData ? "OK" : "FAIL");
        LOG_INFO("GPS RAW: lat_i=%ld lon_i=%ld sats=%d hdop=%dcm hasLock=%s", 
                gps->p.latitude_i, gps->p.longitude_i, gps->p.sats_in_view, gps->p.HDOP, 
                gps->hasLock() ? "YES" : "NO");
        LOG_INFO("GPS CALC: lat=%.8f lon=%.8f age=%ums", 
                gps->p.latitude_i * 1e-7, gps->p.longitude_i * 1e-7,
                fusionData.last_gps_ms > 0 ? (now_ms - fusionData.last_gps_ms) : 0);
        
        // Show detailed validation results with new lenient logic
        bool finalResult = dataValid && qualityOk;
        LOG_INFO("GPS VALIDATION: dataValid=%s qualityOk=%s firstTime=%s FINAL=%s", 
                dataValid ? "YES" : "NO",
                qualityOk ? "YES" : "NO", 
                (fusionData.last_gps_ms == 0) ? "YES" : "NO",
                finalResult ? "PASS" : "FAIL");
        LOG_INFO("GPS LENIENT: Removed hasLock requirement, accepting 1+ sats, any HDOP");
    }
    
    // Very lenient validation for T-Beam Supreme testing
    bool finalResult = dataValid && qualityOk;
    return finalResult;
}

bool GPSIMUFusion::isIMUDataValid() {
    if (!g_qmi8658Live.initialized) return false;
    
    uint32_t now_ms = millis();
    bool recentData = (now_ms - g_qmi8658Live.last_ms) < IMU_TIMEOUT_MS;
    
    return recentData;
}

float GPSIMUFusion::normalizeAngle(float angle) {
    while (angle >= 360.0f) angle -= 360.0f;
    while (angle < 0.0f) angle += 360.0f;
    return angle;
}

void GPSIMUFusion::lowPassFilter(float& filtered, float new_value, float alpha) {
    filtered = alpha * new_value + (1.0f - alpha) * filtered;
}

void GPSIMUFusion::reset() {
    // Reset GPS state
    gps_state.lat_filtered = 0.0;
    gps_state.lon_filtered = 0.0;
    gps_state.alt_filtered = 0.0;
    gps_state.course_filtered = 0.0f;
    gps_state.speed_filtered = 0.0f;
    gps_state.last_course_ms = 0;
    gps_state.moving = false;
    
    // Reset IMU state
    imu_state.velocity = (FusionVector){.axis = {0, 0, 0}};
    imu_state.position = (FusionVector){.axis = {0, 0, 0}};
    imu_state.initialized = false;
    
    // Reset fusion data
    fusionData.gps_valid = false;
    fusionData.imu_valid = false;
    fusionData.last_gps_ms = 0;
    fusionData.last_imu_ms = 0;
    fusionData.last_fusion_ms = 0;
    
    LOG_INFO("GPS+IMU Fusion reset");
}

void GPSIMUFusion::logFusionDataDetailed() {
    if (!fusionData.initialized) {
        LOG_INFO("GPS+IMU Fusion: Not initialized");
        return;
    }
    
    uint32_t now_ms = millis();
    
    LOG_INFO("=== GPS+IMU FUSION DEBUG ===");
    LOG_INFO("Status: GPS=%s IMU=%s Initialized=%s", 
            fusionData.gps_valid ? "VALID" : "INVALID",
            fusionData.imu_valid ? "VALID" : "INVALID", 
            fusionData.initialized ? "YES" : "NO");
    
    if (fusionData.gps_valid || fusionData.imu_valid) {
        // Position data with maximum precision display
        LOG_INFO("Position: %.8f°, %.8f°, %.1fm", 
                fusionData.latitude, fusionData.longitude, fusionData.altitude);
        
        // Velocity data  
        LOG_INFO("Velocity: N=%.2f E=%.2f D=%.2f m/s (Speed=%.2f m/s)", 
                fusionData.velocity_north, fusionData.velocity_east, 
                fusionData.velocity_down, fusionData.speed);
        
        // Orientation data (Madgwick filter output)
        LOG_INFO("Orientation: Roll=%.1f° Pitch=%.1f° Yaw=%.1f°", 
                fusionData.roll, fusionData.pitch, fusionData.yaw);
        
        // Quality indicators
        LOG_INFO("Quality: HDOP=%.2f Sats=%d HeadingAcc=%.1f°", 
                fusionData.hdop, fusionData.satellites, fusionData.heading_accuracy);
        
        // GPS state details
        if (fusionData.gps_valid) {
            LOG_INFO("GPS State: Moving=%s Speed=%.2f Course=%.1f° (filtered)", 
                    gps_state.moving ? "YES" : "NO",
                    gps_state.speed_filtered, gps_state.course_filtered);
        }
        
        // IMU state details
        if (fusionData.imu_valid) {
            LOG_INFO("IMU State: AccX=%.2f AccY=%.2f AccZ=%.2f", 
                    g_qmi8658Live.acc.x, g_qmi8658Live.acc.y, g_qmi8658Live.acc.z);
            LOG_INFO("IMU State: GyrX=%.2f GyrY=%.2f GyrZ=%.2f", 
                    g_qmi8658Live.gyr.x, g_qmi8658Live.gyr.y, g_qmi8658Live.gyr.z);
        }
        
        // Timing information
        LOG_INFO("Timing: GPS=%ums IMU=%ums Fusion=%ums ago", 
                now_ms - fusionData.last_gps_ms, 
                now_ms - fusionData.last_imu_ms,
                now_ms - fusionData.last_fusion_ms);
    } else {
        LOG_INFO("No valid sensor data available");
    }
    
    LOG_INFO("=== END FUSION DEBUG ===");
}

void GPSIMUFusion::logFusionDataQuick() {
    if (!fusionData.initialized) {
        return;
    }
    
    if (fusionData.gps_valid || fusionData.imu_valid) {
        LOG_INFO("FUSION: Pos(%.8f,%.8f) Spd=%.3fm/s Hdg=%.1f° GPS=%s IMU=%s", 
                fusionData.latitude, fusionData.longitude, 
                fusionData.speed, fusionData.yaw,
                fusionData.gps_valid ? "OK" : "FAIL",
                fusionData.imu_valid ? "OK" : "FAIL");
    } else {
        LOG_INFO("FUSION: No valid data - GPS=%s IMU=%s (Check GPS lock and IMU init)", 
                fusionData.gps_valid ? "OK" : "FAIL",
                fusionData.imu_valid ? "OK" : "FAIL");
    }
}

#endif // !MESHTASTIC_EXCLUDE_GPS
