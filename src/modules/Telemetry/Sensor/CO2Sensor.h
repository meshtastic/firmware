#pragma once

#include "MeshModule.h"

/*
Shared CO2 calibration interface + admin-message dispatch for any sensor that
exposes Sensirion-style CO2 auto/forced calibration: automatic self-calibration
(ASC), forced recalibration (FRC), altitude/ambient-pressure compensation, and
a calibration-history factory reset. SCD4XSensor, SCD30Sensor and the
CO2-capable SEN6X variants (SEN63C/SEN66/SEN69C, via SENXXSensor) all implement
this instead of duplicating the same admin-message branching logic.

Concrete classes only need to implement the low-level co2* operations against
their own I2C command set; handleCo2AdminRequest() below is the one shared
place that decides *when* to call FRC vs ASC, validates that a target CO2 was
supplied for FRC, and reverts ASC on a failed FRC attempt.
*/
class CO2CalibrationSensor
{
  protected:
    virtual ~CO2CalibrationSensor() {}

    // Forced recalibration against a known reference CO2 concentration (ppm).
    virtual bool co2PerformFRC(uint32_t targetCO2ppm) = 0;

    // Automatic self-calibration on/off.
    virtual bool co2GetASC(bool &ascEnabled) = 0;
    virtual bool co2SetASC(bool ascEnabled) = 0;
    // Optional: not every sensor exposes a settable ASC baseline (e.g. SCD30/SEN6X don't).
    virtual bool co2SetASCBaseline(uint32_t targetCO2ppm) { return true; }

    // Altitude/pressure compensation. altitude in meters above sea level,
    // ambientPressure in Pa (implementations convert to whatever unit their
    // own command set expects).
    virtual bool co2SetAltitude(uint32_t altitude) = 0;
    virtual bool co2SetAmbientPressure(uint32_t ambientPressurePa) { return false; }

    // Erases the sensor's FRC/ASC calibration history. Optional.
    virtual bool co2FactoryReset() { return false; }

    // Snapshot of whichever *_config admin message fields were populated,
    // translated once by the caller into this sensor-agnostic shape.
    struct Co2AdminRequest {
        bool hasFactoryReset = false;
        bool hasSetAsc = false;
        bool setAsc = false;
        bool hasTargetCo2 = false;
        uint32_t targetCo2 = 0;
        bool hasSetAltitude = false;
        uint32_t setAltitude = 0;
        bool hasSetAmbientPressure = false;
        uint32_t setAmbientPressure = 0;
    };

    // Returns false if a requested operation failed - callers should map
    // that to AdminMessageHandleResult::NOT_HANDLED like they already do for
    // their sensor-specific fields (e.g. temperature offset, power mode).
    bool handleCo2AdminRequest(const Co2AdminRequest &cfg, const char *sensorName)
    {
        if (cfg.hasFactoryReset) {
            LOG_DEBUG("%s: Requested CO2 calibration factory reset", sensorName);
            return co2FactoryReset();
        }

        if (cfg.hasSetAsc) {
            if (!cfg.setAsc) {
                bool currentASC = false;
                if (!co2GetASC(currentASC)) {
                    return false;
                }
                // Disabling ASC is how you request a forced recalibration (FRC).
                if (!cfg.hasTargetCo2) {
                    LOG_ERROR("%s: target CO2 not provided for FRC", sensorName);
                    return false;
                }
                LOG_DEBUG("%s: Request for FRC", sensorName);
                if (!co2SetASC(false)) {
                    return false;
                }
                if (!co2PerformFRC(cfg.targetCo2)) {
                    // Restore previous ASC state since the FRC attempt failed.
                    co2SetASC(currentASC);
                    return false;
                }
            } else {
                LOG_DEBUG("%s: Request for ASC", sensorName);
                if (!co2SetASC(true)) {
                    return false;
                }
                // ASC with target CO2 is only available in SCD4X
                if (cfg.hasTargetCo2) {
                    if (!co2SetASCBaseline(cfg.targetCo2)) {
                        return false;
                    }
                }
            }
        }

        if (cfg.hasSetAltitude) {
            if (!co2SetAltitude(cfg.setAltitude)) {
                return false;
            }
        } else if (cfg.hasSetAmbientPressure) {
            if (!co2SetAmbientPressure(cfg.setAmbientPressure)) {
                return false;
            }
        }

        return true;
    }
};
