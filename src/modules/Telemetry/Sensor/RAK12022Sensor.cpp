/*
 * RAK12022 PT100 Temperature Sensor Driver for Meshtastic
 * 
 * Copyright (c) 2025 Commissioned by JFK
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(RAK12022_ADDR)
#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "RAK12022Sensor.h"
#include "TelemetrySensor.h"
#include <SPI.h>

// ========== USER CONFIGURATION ==========

// Temperature calibration offset in Celsius (adjust if readings are consistently off)
#define TEMP_OFFSET_C  0.0

// PT100 sensor wire configuration - set to 2, 3, or 4
// 2-wire: simplest, less accurate
// 3-wire: good accuracy, compensates for lead resistance (default)
// 4-wire: best accuracy, full lead resistance compensation
#define SENSOR_WIRES  3

// Power line filter frequency - set to 50 or 60 based on your region
// North America: 60Hz, Europe/Asia: 50Hz
#define FILTER_HZ  60

// Fault thresholds - sensor will flag readings outside this range
// These values are in RTD register format (not temperature)
// Current settings: 10F to 200F (-12C to 93C)
#define FAULT_THRESHOLD_LOW_MSB   0x1C
#define FAULT_THRESHOLD_LOW_LSB   0x5D
#define FAULT_THRESHOLD_HIGH_MSB  0x28
#define FAULT_THRESHOLD_HIGH_LSB  0x70

// PT100 calibration - adjust RREF if temperature readings are incorrect across full range
// Standard value is 430, but calibration showed 402 for this module
#define RREF      402.0
#define RNOMINAL  100.0

// ========== END USER CONFIGURATION ==========

// Power and CS pins
#define RAK12022_POWER_PIN 35  // WB_IO2
#define RAK12022_CS_PIN    26  // P0.26

// SPI1 pins (CS=26, MOSI=30, MISO=29, SCK=3)
#define RAK_MOSI  30
#define RAK_MISO  29
#define RAK_SCK   3

// MAX31865 registers
#define MAX31865_CONFIG_REG      0x00
#define MAX31865_RTDMSB_REG      0x01
#define MAX31865_RTDLSB_REG      0x02
#define MAX31865_HFAULTMSB_REG   0x03
#define MAX31865_HFAULTLSB_REG   0x04
#define MAX31865_LFAULTMSB_REG   0x05
#define MAX31865_LFAULTLSB_REG   0x06
#define MAX31865_FAULTSTAT_REG   0x07

// MAX31865 Configuration bits
#define MAX31865_CONFIG_BIAS        0x80
#define MAX31865_CONFIG_MODEAUTO    0x40
#define MAX31865_CONFIG_3WIRE       0x10
#define MAX31865_CONFIG_2WIRE       0x00  // 2-wire or 4-wire mode (bit 4 = 0)
#define MAX31865_CONFIG_FILT60HZ    0x00  // 60Hz filter (bit 0 = 0)
#define MAX31865_CONFIG_FILT50HZ    0x01  // 50Hz filter (bit 0 = 1)

// PT100 parameters
#define RNOMINAL  100.0

// Create SPI instance using SPI1
SPIClass SPI_RAK(NRF_SPIM1, RAK_MISO, RAK_SCK, RAK_MOSI);

RAK12022Sensor::RAK12022Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_CUSTOM_SENSOR, "RAK12022") {}

uint8_t RAK12022Sensor::read8(uint8_t reg) {
    SPI_RAK.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE3));
    digitalWrite(RAK12022_CS_PIN, LOW);
    delayMicroseconds(1);
    SPI_RAK.transfer(reg & 0x7F);
    uint8_t value = SPI_RAK.transfer(0x00);
    delayMicroseconds(5);
    digitalWrite(RAK12022_CS_PIN, HIGH);
    SPI_RAK.endTransaction();
    return value;
}

uint16_t RAK12022Sensor::read16(uint8_t reg) {
    SPI_RAK.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE3));
    digitalWrite(RAK12022_CS_PIN, LOW);
    delayMicroseconds(1);
    SPI_RAK.transfer(reg & 0x7F);
    uint8_t msb = SPI_RAK.transfer(0x00);
    uint8_t lsb = SPI_RAK.transfer(0x00);
    delayMicroseconds(5);
    digitalWrite(RAK12022_CS_PIN, HIGH);
    SPI_RAK.endTransaction();
    return ((uint16_t)msb << 8) | lsb;
}

void RAK12022Sensor::write8(uint8_t reg, uint8_t value) {
    SPI_RAK.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE3));
    digitalWrite(RAK12022_CS_PIN, LOW);
    delayMicroseconds(1);
    SPI_RAK.transfer(0x80 | (reg & 0x7F));
    SPI_RAK.transfer(value);
    delayMicroseconds(5);
    digitalWrite(RAK12022_CS_PIN, HIGH);
    SPI_RAK.endTransaction();
}

int32_t RAK12022Sensor::runOnce()
{
    LOG_INFO("RAK12022: Initializing PT100 sensor using SPI1\n");
    
    // Power up sensor
    pinMode(RAK12022_POWER_PIN, OUTPUT);
    digitalWrite(RAK12022_POWER_PIN, HIGH);
    delay(300);
    
    // Setup CS
    pinMode(RAK12022_CS_PIN, OUTPUT);
    digitalWrite(RAK12022_CS_PIN, HIGH);
    delay(10);
    
    // Initialize SPI1
    SPI_RAK.begin();
    delay(10);
    
    // Configure for PT100 with selected wire configuration and filter frequency
    uint8_t wires = (SENSOR_WIRES == 3) ? MAX31865_CONFIG_3WIRE : MAX31865_CONFIG_2WIRE;
    uint8_t filter = (FILTER_HZ == 50) ? MAX31865_CONFIG_FILT50HZ : MAX31865_CONFIG_FILT60HZ;
    uint8_t config = MAX31865_CONFIG_BIAS | MAX31865_CONFIG_MODEAUTO | wires | filter;
    
    write8(MAX31865_CONFIG_REG, config);
    delay(10);
    
    uint8_t readback = read8(MAX31865_CONFIG_REG);
    if (readback != config) {
        LOG_ERROR("RAK12022: Config failed\n");
        status = false;
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    
    LOG_INFO("RAK12022: Config verified!\n");
    
    // Set fault thresholds
    LOG_INFO("RAK12022: Setting fault thresholds (-12C to 93C)\n");
    write8(MAX31865_LFAULTMSB_REG, FAULT_THRESHOLD_LOW_MSB);
    write8(MAX31865_LFAULTLSB_REG, FAULT_THRESHOLD_LOW_LSB);
    write8(MAX31865_HFAULTMSB_REG, FAULT_THRESHOLD_HIGH_MSB);
    write8(MAX31865_HFAULTLSB_REG, FAULT_THRESHOLD_HIGH_LSB);
    delay(10);
    
    // Clear faults
    uint8_t cfg = read8(MAX31865_CONFIG_REG);
    write8(MAX31865_CONFIG_REG, cfg | 0x02);
    delay(10);
    write8(MAX31865_CONFIG_REG, cfg);
    delay(50);
    
    // Wait for conversion
    delay(1000);
    
    // Read temperature
    uint16_t rtd = read16(MAX31865_RTDMSB_REG);
    
    if (rtd == 0x0000 || rtd == 0xFFFF) {
        LOG_ERROR("RAK12022: Invalid RTD\n");
        status = false;
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    
    rtd >>= 1;
    float resistance = ((float)rtd * RREF) / 32768.0;
    float tempC = (resistance - RNOMINAL) / (RNOMINAL * 0.00385);
    tempC = tempC + TEMP_OFFSET_C;
    
    LOG_INFO("RAK12022: %.1fC, R=%.2f Ohm\n", tempC, resistance);
    
    if (tempC >= -50.0 && tempC <= 200.0 && !isnan(tempC)) {
        LOG_INFO("RAK12022: Sensor initialized successfully!\n");
        status = true;
    } else {
        LOG_ERROR("RAK12022: Invalid temperature\n");
        status = false;
    }
    
    return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
}

void RAK12022Sensor::setup() {}

bool RAK12022Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    if (!status) return false;
    
    uint16_t rtd = read16(MAX31865_RTDMSB_REG);
    
    // Only fail on completely invalid readings
    if (rtd == 0x0000 || rtd == 0xFFFF) {
        return false;
    }
    
    // Remove fault bit and calculate temperature
    rtd >>= 1;
    float resistance = ((float)rtd * RREF) / 32768.0;
    float tempC = (resistance - RNOMINAL) / (RNOMINAL * 0.00385);
    tempC = tempC + TEMP_OFFSET_C;
    
    if (tempC < -50.0 || tempC > 200.0 || isnan(tempC)) {
        return false;
    }
    
    LOG_INFO("RAK12022: %.1fC\n", tempC);
    
    measurement->variant.environment_metrics.temperature = tempC;
    measurement->variant.environment_metrics.has_temperature = true;
    
    return true;
}

#endif
