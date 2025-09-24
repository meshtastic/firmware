#include "GPSIMUFusion.h"
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_GPS

#include "gps/GPS.h"
#include "motion/SensorLiveData.h"
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
    float base_alpha = 0.4f;
    float gps_alpha = base_alpha;
    
    // Get GPS quality indicators
    float hdop_m = gps->p.HDOP / 100.0f; // cm -> m
    uint8_t sats = gps->p.sats_in_view;
    
    if (hdop_m > 5.0f || sats < 4) {
        gps_alpha = 0.2f; // heavier filtering
    } else if (hdop_m < 2.0f && sats >= 6) {
        gps_alpha = 0.6f; // lighter filtering
    }
    
    if (gps_state.lat_filtered == 0.0 && gps_state.lon_filtered == 0.0) {
        gps_state.lat_filtered = gps_lat;
        gps_state.lon_filtered = gps_lon;
        gps_state.alt_filtered = gps_alt;
        LOG_INFO("GPS INIT: lat=%.8f lon=%.8f alt=%.1f (hdop=%.1fm sats=%d)", gps_lat, gps_lon, gps_alt, hdop_m, sats);
    } else {
        double lat_diff = gps_lat - gps_state.lat_filtered;
        double lon_diff = gps_lon - gps_state.lon_filtered;
        double distance_deg = sqrt(lat_diff*lat_diff + lon_diff*lon_diff);
        double distance_m = distance_deg * 111320.0; // approx meters
        
        float max_jump = (hdop_m > 10.0f) ? 200.0f : 50.0f;
        if (distance_m > max_jump) {
            LOG_INFO("GPS RESET: jump %.1fm > %.1fm, resetting", distance_m, max_jump);
            gps_state.lat_filtered = gps_lat;
            gps_state.lon_filtered = gps_lon;
            gps_state.alt_filtered = gps_alt;
        } else {
            float lat_f = (float)gps_lat;
            float lon_f = (float)gps_lon;
            float lat_filtered_f = (float)gps_state.lat_filtered;
            float lon_filtered_f = (float)gps_state.lon_filtered;
            
            lowPassFilter(lat_filtered_f, lat_f, gps_alpha);
            lowPassFilter(lon_filtered_f, lon_f, gps_alpha);
            lowPassFilter(gps_state.alt_filtered, gps_alt, gps_alpha);
            
            gps_state.lat_filtered = (double)lat_filtered_f;
            gps_state.lon_filtered = (double)lon_filtered_f;
        }
    }
    
    // Velocity/course
    float gps_speed = 0.0f;
    float gps_course = 0.0f;
    
    if (gps->p.has_ground_speed) {
        gps_speed = gps->p.ground_speed / 3.6f; // km/h -> m/s
    }
    if (gps->p.has_ground_track) {
        gps_course = gps->p.ground_track / 1e5f;
        
        gps_state.moving = (gps_speed > GPS_VELOCITY_THRESHOLD);
        if (gps_state.moving) {
            if (gps_state.last_course_ms == 0) {
                gps_state.course_filtered = gps_course;
            } else {
                float diff = gps_course - gps_state.course_filtered;
                if (diff > 180.0f) diff -= 360.0f;
                if (diff < -180.0f) diff += 360.0f;
                gps_state.course_filtered += diff * 0.2f;
                gps_state.course_filtered = normalizeAngle(gps_state.course_filtered);
            }
            gps_state.last_course_ms = now_ms;
        }
        lowPassFilter(gps_state.speed_filtered, gps_speed, 0.4f);
    }
    
    fusionData.hdop = gps->p.HDOP / 100.0f;
    fusionData.satellites = gps->p.sats_in_view;
    
    if (gps_state.moving && gps_speed > 2.0f) {
        fusionData.heading_accuracy = constrain(5.0f / gps_speed + fusionData.hdop, 2.0f, 45.0f);
    } else {
        fusionData.heading_accuracy = 180.0f;
    }
}

void GPSIMUFusion::fuseNavigationData(float dt) {
    if (fusionData.gps_valid) {
        fusionData.latitude = gps_state.lat_filtered;
        fusionData.longitude = gps_state.lon_filtered;
        fusionData.altitude = gps_state.alt_filtered;
        
        if (fusionData.imu_valid) {
            float gps_weight = 0.1f;
            if (gps_state.moving) {
                float course_rad = gps_state.course_filtered * M_PI / 180.0f;
                float gps_vel_north = gps_state.speed_filtered * cos(course_rad);
                float gps_vel_east = gps_state.speed_filtered * sin(course_rad);
                fusionData.velocity_north = (1.0f - gps_weight) * imu_state.velocity.axis.x + gps_weight * gps_vel_north;
                fusionData.velocity_east = (1.0f - gps_weight) * imu_state.velocity.axis.y + gps_weight * gps_vel_east;
                imu_state.velocity.axis.x = fusionData.velocity_north;
                imu_state.velocity.axis.y = fusionData.velocity_east;
            }
        }
        fusionData.speed = gps_state.speed_filtered;
    }
    
    if (fusionData.gps_valid && gps_state.moving && fusionData.heading_accuracy < 20.0f) {
        float heading_weight = constrain(1.0f / (fusionData.heading_accuracy / 10.0f), 0.1f, 0.8f);
        float yaw_diff = gps_state.course_filtered - fusionData.yaw;
        if (yaw_diff > 180.0f) yaw_diff -= 360.0f;
        if (yaw_diff < -180.0f) yaw_diff += 360.0f;
        fusionData.yaw += yaw_diff * heading_weight;
        fusionData.yaw = normalizeAngle(fusionData.yaw);
    }
}

bool GPSIMUFusion::isGPSDataValid() {
    if (!gps) return false;
    uint32_t now_ms = millis();
    bool hasLock = gps->hasLock();
    bool hasPositionData = (gps->p.latitude_i != 0 || gps->p.longitude_i != 0);
    bool coordinatesReasonable = (abs(gps->p.latitude_i) <= 900000000 && abs(gps->p.longitude_i) <= 1800000000);
    bool hasMinSats = (gps->p.sats_in_view >= 1);
    bool hasHDOP = (gps->p.HDOP >= 0);
    bool dataValid = hasPositionData && coordinatesReasonable; // lenient for testing
    bool qualityOk = hasMinSats || hasHDOP || (fusionData.last_gps_ms == 0);
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
    gps_state.lat_filtered = 0.0;
    gps_state.lon_filtered = 0.0;
    gps_state.alt_filtered = 0.0;
    gps_state.course_filtered = 0.0f;
    gps_state.speed_filtered = 0.0f;
    gps_state.last_course_ms = 0;
    gps_state.moving = false;
    
    imu_state.velocity = (FusionVector){.axis = {0, 0, 0}};
    imu_state.position = (FusionVector){.axis = {0, 0, 0}};
    imu_state.initialized = false;
    
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
        LOG_INFO("Position: %.8f°, %.8f°, %.1fm", fusionData.latitude, fusionData.longitude, fusionData.altitude);
        LOG_INFO("Velocity: N=%.2f E=%.2f D=%.2f m/s (Speed=%.2f m/s)", 
                fusionData.velocity_north, fusionData.velocity_east, 
                fusionData.velocity_down, fusionData.speed);
        LOG_INFO("Orientation: Roll=%.1f° Pitch=%.1f° Yaw=%.1f°", 
                fusionData.roll, fusionData.pitch, fusionData.yaw);
        LOG_INFO("Quality: HDOP=%.2f Sats=%d HeadingAcc=%.1f°", 
                fusionData.hdop, fusionData.satellites, fusionData.heading_accuracy);
        if (fusionData.gps_valid) {
            LOG_INFO("GPS State: Moving=%s Speed=%.2f Course=%.1f° (filtered)", 
                    gps_state.moving ? "YES" : "NO",
                    gps_state.speed_filtered, gps_state.course_filtered);
        }
        if (fusionData.imu_valid) {
            LOG_INFO("IMU State: AccX=%.2f AccY=%.2f AccZ=%.2f", 
                    g_qmi8658Live.acc.x, g_qmi8658Live.acc.y, g_qmi8658Live.acc.z);
            LOG_INFO("IMU State: GyrX=%.2f GyrY=%.2f GyrZ=%.2f", 
                    g_qmi8658Live.gyr.x, g_qmi8658Live.gyr.y, g_qmi8658Live.gyr.z);
        }
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
