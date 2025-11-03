# GPS+IMU Fusion Debug Guide

## Overview

The GPS+IMU fusion system includes comprehensive debug logging to help you monitor and troubleshoot the Madgwick filter output and sensor fusion performance.

## Automatic Logging

The fusion system **automatically logs data** when active:

- **Quick Status**: Every 1 second - Shows position, speed, heading, and sensor status
- **Detailed Debug**: Every 5 seconds - Shows complete fusion state, raw sensor data, timing, and quality metrics

### Quick Status Log Example:
```
FUSION: Pos(37.12345,-122.45678) Spd=2.3m/s Hdg=45° GPS=OK IMU=OK
```

### Detailed Debug Log Example:
```
=== GPS+IMU FUSION DEBUG ===
Status: GPS=VALID IMU=VALID Initialized=YES
Position: 37.123456°, -122.456789°, 123.4m
Velocity: N=1.23 E=2.34 D=0.00 m/s (Speed=2.67 m/s)  
Orientation: Roll=2.1° Pitch=-1.5° Yaw=45.3°
Quality: HDOP=1.20 Sats=8 HeadingAcc=3.2°
GPS State: Moving=YES Speed=2.67 Course=45.3° (filtered)
IMU State: AccX=0.12 AccY=0.34 AccZ=9.81
IMU State: GyrX=0.02 GyrY=-0.01 GyrZ=0.15
Timing: GPS=234ms IMU=45ms Fusion=12ms ago
=== END FUSION DEBUG ===
```

## Manual Debug Functions

### Option 1: Include Debug Header (Recommended)
```cpp
#include "Fusion/GPSIMUFusionDebug.h"

// In your code:
DEBUG_FUSION_NOW();     // Detailed debug output immediately
DEBUG_FUSION_QUICK();   // Quick status check
```

### Option 2: Direct Function Calls
```cpp
#include "Fusion/GPSIMUFusion.h"

// Force immediate debug output
debugGPSIMUFusionNow();        // Detailed debug
quickGPSIMUFusionStatus();     // Quick status
demonstrateGPSIMUFusion();     // Demo function
```

### Option 3: Access Data Directly
```cpp
#include "motion/SensorLiveData.h"

const GPSIMUFusionData* fusion = getGPSIMUFusionData();
if (fusion && fusion->initialized) {
    LOG_INFO("Position: %.6f, %.6f", fusion->latitude, fusion->longitude);
    LOG_INFO("Madgwick Yaw: %.1f°", fusion->yaw);
    LOG_INFO("Speed: %.2f m/s", fusion->speed);
}
```

## Helper Functions

### Check System Status
```cpp
if (FUSION_IS_AVAILABLE()) {
    // Fusion system is working
}
```

### Get Position
```cpp
double lat, lon;
if (getFusionPosition(&lat, &lon)) {
    LOG_INFO("Current position: %.6f, %.6f", lat, lon);
}
```

### Get Orientation (Madgwick Output)
```cpp
float roll, pitch, yaw;
if (getFusionOrientation(&roll, &pitch, &yaw)) {
    LOG_INFO("Orientation: R=%.1f P=%.1f Y=%.1f", roll, pitch, yaw);
}
```

### Get Speed
```cpp
float speed = getFusionSpeed();
if (speed >= 0) {
    LOG_INFO("Speed: %.2f m/s", speed);
}
```

## Understanding the Output

### Position Data
- **Source**: GPS (absolute reference) + IMU (smoothing)
- **Format**: Decimal degrees for lat/lon, meters for altitude
- **Accuracy**: Depends on GPS HDOP and satellite count

### Velocity Data
- **Source**: GPS velocity + IMU acceleration integration
- **Components**: North/East/Down in m/s
- **Speed**: Horizontal speed magnitude

### Orientation Data (Madgwick Filter)
- **Roll**: Rotation around forward axis (-180 to +180°)
- **Pitch**: Rotation around right axis (-90 to +90°)  
- **Yaw**: Heading/direction (0-360°, 0=North)
- **Source**: IMU fusion (gyro + accel + mag) + GPS course when moving

### Quality Indicators
- **HDOP**: Horizontal Dilution of Precision (lower = better GPS accuracy)
  - < 2.0: Excellent
  - 2.0-5.0: Good  
  - > 5.0: Poor
- **Satellites**: Number of GPS satellites in view
- **Heading Accuracy**: Estimated heading error in degrees

### GPS State
- **Moving**: Vehicle speed > 1.0 m/s threshold
- **Speed**: Filtered GPS speed in m/s
- **Course**: Filtered GPS course in degrees (true north)

### IMU State  
- **Accelerometer**: X/Y/Z acceleration in m/s²
- **Gyroscope**: X/Y/Z angular velocity in degrees/second
- **Values**: Raw sensor readings fed to Madgwick filter

### Timing
- **GPS**: Milliseconds since last GPS update
- **IMU**: Milliseconds since last IMU update  
- **Fusion**: Milliseconds since last fusion calculation

## Troubleshooting

### No Debug Output
- Check that GPS is not excluded: `#if !MESHTASTIC_EXCLUDE_GPS`
- Verify accelerometer thread is running
- Ensure sensors are detected and initialized

### GPS=INVALID
- No GPS lock or poor signal
- Check antenna connection
- Move to open sky area
- Wait for GPS initialization (can take 30s-2min)

### IMU=INVALID  
- IMU sensor not detected or failed
- Check I2C/SPI connections
- Verify sensor is properly powered
- Check for compatible IMU chip

### Poor Orientation
- Magnetometer interference (check away from metal)
- IMU not properly calibrated
- Device orientation doesn't match expected frame
- High vibration environment

### Position Jumps
- Poor GPS signal causing multipath
- GPS accuracy degraded (high HDOP)
- Switch between GPS and dead reckoning modes

## Performance Impact

- **CPU**: ~1-2% additional overhead
- **Memory**: ~500 bytes for fusion state  
- **Logging**: Can be disabled by commenting out calls in `fuseNavigationData()`
- **Update Rate**: 50-100 Hz (matches accelerometer thread)

## Customization

### Adjust Log Frequency
Edit `src/Fusion/GPSIMUFusion.cpp`:
```cpp
// Change logging intervals
if (now_ms - lastDetailedLog > 10000) { // 10 seconds instead of 5
if (now_ms - lastQuickLog > 2000) {     // 2 seconds instead of 1
```

### Disable Auto Logging
Comment out the logging calls in `fuseNavigationData()`:
```cpp
// logFusionDataDetailed();
// logFusionDataQuick();
```

### Add Custom Logging
```cpp
if (fusion->speed > 5.0f) {  // Log only when moving fast
    DEBUG_FUSION_QUICK();
}
```

The debug system provides complete visibility into the GPS+IMU sensor fusion process and Madgwick filter performance!
