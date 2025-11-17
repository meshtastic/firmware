#pragma once

#include "TelemetrySensor.h"
#include "meshtastic/telemetry.pb.h"   // For meshtastic_Telemetry struct

/**
 * @brief Reads analog soil moisture levels (e.g. capacitive or resistive sensor)
 * and reports them as a percentage (0–100%).
 */
class SoilMoistureSensor : public TelemetrySensor {
public:
    SoilMoistureSensor();

    // Called once to initialize sensor or check connection
    int32_t runOnce() override;

    // Called to fill telemetry message with current readings
    bool getMetrics(meshtastic_Telemetry *measurement) override;

private:
    bool initialized = false;

    // Example variable for moisture percentage
    float soilMoisturePercent = 0.0f;

    // Reads analog input and converts to moisture percent
    bool readSensorData();

    // Replace with your board’s analog pin
    static constexpr int ANALOG_PIN = 34; // Example: GPIO34 on ESP32
};


#pragma once
#include "TelemetrySensor.h"

class SoilMoistureSensor : public TelemetrySensor {
  public:
    SoilMoistureSensor();
    
    // Required TelemetrySensor overrides - EXACTLY like other sensors
    int32_t runOnce() override;
    bool getMetrics(meshtastic_Telemetry *measurement) override;
    
  private:
    bool readSensor();
    bool initialized = false;
    
    // Sensor data
    float soilMoisture = 0.0;      // Percentage (0-100%)
    float soilTemperature = 0.0;   // Celsius
    uint16_t rawMoisture = 0;      // Raw sensor value
    
    // Configuration
    uint8_t sensorType = 0;        // 0=Analog, 1=I2C_Seesaw, 2=I2C_SparkFun
    uint8_t analogPin = A0;        // For analog sensors
    
    // I2C addresses
    static const uint8_t SEESAW_ADDR = 0x36;
    static const uint8_t SPARKFUN_ADDR = 0x28;
};