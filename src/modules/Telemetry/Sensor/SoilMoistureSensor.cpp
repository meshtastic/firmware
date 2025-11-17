#include "SoilMoistureSensor.h"
#include "logging/Logger.h"

//#include "SoilMoistureSensor.h"
#include "graphics/ScreenFonts.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"

#ifdef ARDUINO_ARCH_ESP32
#include "driver/adc.h"  // for analogRead on ESP32
#endif

// Constructor
SoilMoistureSensor::SoilMoistureSensor()
    : TelemetrySensor("SoilMoisture", "Soil moisture analog sensor") {
    LOG_DEBUG("SoilMoistureSensor: constructor\n");
}

int32_t SoilMoistureSensor::runOnce() {
    LOG_DEBUG("SoilMoistureSensor: initializing...\n");

    // For analog sensor, no I2C detection needed
    initialized = true;
    enabled = true;

    LOG_INFO("SoilMoistureSensor: initialized successfully\n");

    // Read every 60 seconds (60000 ms)
    return 60000;
}

bool SoilMoistureSensor::getMetrics(meshtastic_Telemetry *measurement) {
    if (!initialized || !enabled) {
        LOG_DEBUG("SoilMoistureSensor: not initialized or not enabled\n");
        return false;
    }

    if (!readSensorData()) {
        LOG_ERROR("SoilMoistureSensor: failed to read sensor data\n");
        return false;
    }

    // Assign to environment metrics (protobuf field)
    measurement->variant.environment_metrics.soil_moisture = soilMoisturePercent;
    measurement->variant.environment_metrics.has_soil_moisture = true;

    LOG_INFO("SoilMoistureSensor: moisture = %.1f%%\n", soilMoisturePercent);
    return true;
}

bool SoilMoistureSensor::readSensorData() {
    // Read analog value (0–4095 on ESP32 ADC)
    int rawValue = analogRead(ANALOG_PIN);

    // Map raw value to 0–100% moisture (adjust min/max to your sensor’s calibration)
    const int DRY_READING = 3500;   // adjust based on your sensor
    const int WET_READING = 1500;   // adjust based on your sensor

    rawValue = std::min(std::max(rawValue, WET_READING), DRY_READING);

    // Convert to percentage
    soilMoisturePercent = 100.0f * (DRY_READING - rawValue) / (DRY_READING - WET_READING);

    LOG_DEBUG("SoilMoistureSensor: raw=%d, moisture=%.1f%%\n", rawValue, soilMoisturePercent);
    return true;
}



// Check for supported sensor libraries - EXACT PATTERN as other sensors
#if __has_include(<Adafruit_Seesaw.h>)
#include <Adafruit_Seesaw.h>
Adafruit_Seesaw seesaw;
#define HAS_SEESAW 1
#endif

#if __has_include(<SparkFun_Soil_Moisture_Sensor_Arduino_Library.h>)
#include <SparkFun_Soil_Moisture_Sensor_Arduino_Library.h>
SparkFun_Soil_Moisture_Sensor soilSensor;
#define HAS_SPARKFUN 1
#endif

// Constructor - EXACT PATTERN as BME280Sensor, AHT10, etc.
SoilMoistureSensor::SoilMoistureSensor() : 
    TelemetrySensor("SoilMoisture", "Soil Moisture Sensor") 
{
    // Initialization in runOnce() like all other sensors
}

// runOnce() - EXACT PATTERN as other sensors
int32_t SoilMoistureSensor::runOnce() {
    LOG_DEBUG("SoilMoistureSensor::runOnce()\n");
    
    // If already initialized, just return the read interval
    if (initialized) {
        return 30000; // Read every 30 seconds
    }
    
    // Try I2C sensors first - USING scanI2C() from TelemetrySensor base class
    #ifdef HAS_SEESAW
    if (scanI2C(SEESAW_ADDR)) {
        LOG_DEBUG("SoilMoistureSensor: Found Seesaw sensor at 0x%X\n", SEESAW_ADDR);
        if (seesaw.begin(SEESAW_ADDR)) {
            sensorType = 1;
            initialized = true;
            setFound(true); // IMPORTANT: Mark sensor as found
            LOG_INFO("SoilMoistureSensor: Seesaw initialized successfully\n");
            return 30000; // 30 second read interval
        }
    }
    #endif
    
    #ifdef HAS_SPARKFUN
    if (scanI2C(SPARKFUN_ADDR)) {
        LOG_DEBUG("SoilMoistureSensor: Found SparkFun sensor at 0x%X\n", SPARKFUN_ADDR);
        if (soilSensor.begin(SPARKFUN_ADDR)) {
            sensorType = 2;
            initialized = true;
            setFound(true); // IMPORTANT: Mark sensor as found
            LOG_INFO("SoilMoistureSensor: SparkFun initialized successfully\n");
            return 30000;
        }
    }
    #endif
    
    // Fall back to analog sensor - ALWAYS available
    LOG_DEBUG("SoilMoistureSensor: Using analog sensor on pin A0\n");
    pinMode(analogPin, INPUT);
    sensorType = 0;
    initialized = true;
    setFound(true); // IMPORTANT: Mark sensor as found
    LOG_INFO("SoilMoistureSensor: Analog sensor initialized\n");
    
    return 30000; // Read every 30 seconds
}

// getMetrics() - EXACT PATTERN as other sensors
bool SoilMoistureSensor::getMetrics(meshtastic_Telemetry *measurement) {
    if (!initialized || !isEnabled()) {
        return false;
    }
    
    if (!readSensor()) {
        return false;
    }
    
    // Populate telemetry data - using existing environment metrics fields
    measurement->variant.environment_metrics.temperature = soilTemperature;
    measurement->variant.environment_metrics.relative_humidity = soilMoisture;
    measurement->variant.environment_metrics.has_temperature = true;
    measurement->variant.environment_metrics.has_relative_humidity = true;
    
    LOG_DEBUG("SoilMoistureSensor: Moisture: %.1f%%, Temp: %.1f°C, Raw: %d\n", 
              soilMoisture, soilTemperature, rawMoisture);
    
    return true;
}

// readSensor() - Private helper method like other sensors
bool SoilMoistureSensor::readSensor() {
    if (!initialized) return false;
    
    switch (sensorType) {
        #ifdef HAS_SEESAW
        case 1: // Seesaw sensor
            rawMoisture = seesaw.touchRead(0);
            soilTemperature = seesaw.getTemp();
            // Convert capacitive reading to percentage (adjust calibration as needed)
            soilMoisture = map(constrain(rawMoisture, 200, 2000), 200, 2000, 0, 100);
            break;
        #endif
            
        #ifdef HAS_SPARKFUN  
        case 2: // SparkFun sensor
            rawMoisture = soilSensor.getCapacitance();
            soilTemperature = 0.0; // This sensor doesn't measure temperature
            soilMoisture = map(constrain(rawMoisture, 300, 500), 300, 500, 0, 100);
            break;
        #endif
            
        case 0: // Analog sensor (resistive)
        default:
            rawMoisture = analogRead(analogPin);
            soilTemperature = 0.0;
            // For resistive sensors: lower value = more moisture
            // Calibration: 260=wet, 520=dry (adjust for your sensor)
            soilMoisture = map(constrain(rawMoisture, 260, 520), 260, 520, 100, 0);
            break;
    }
    
    // Constrain to valid range
    soilMoisture = constrain(soilMoisture, 0, 100);
    
    return true;
}
