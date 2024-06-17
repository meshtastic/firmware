/**
 * @file FusionCalibration.h
 * @author Seb Madgwick
 * @brief Gyroscope, accelerometer, and magnetometer calibration models.
 */

#ifndef FUSION_CALIBRATION_H
#define FUSION_CALIBRATION_H

//------------------------------------------------------------------------------
// Includes

#include "FusionMath.h"

//------------------------------------------------------------------------------
// Inline functions

/**
 * @brief Gyroscope and accelerometer calibration model.
 * @param uncalibrated Uncalibrated measurement.
 * @param misalignment Misalignment matrix.
 * @param sensitivity Sensitivity.
 * @param offset Offset.
 * @return Calibrated measurement.
 */
static inline FusionVector FusionCalibrationInertial(const FusionVector uncalibrated, const FusionMatrix misalignment,
                                                     const FusionVector sensitivity, const FusionVector offset)
{
    return FusionMatrixMultiplyVector(misalignment,
                                      FusionVectorHadamardProduct(FusionVectorSubtract(uncalibrated, offset), sensitivity));
}

/**
 * @brief Magnetometer calibration model.
 * @param uncalibrated Uncalibrated measurement.
 * @param softIronMatrix Soft-iron matrix.
 * @param hardIronOffset Hard-iron offset.
 * @return Calibrated measurement.
 */
static inline FusionVector FusionCalibrationMagnetic(const FusionVector uncalibrated, const FusionMatrix softIronMatrix,
                                                     const FusionVector hardIronOffset)
{
    return FusionMatrixMultiplyVector(softIronMatrix, FusionVectorSubtract(uncalibrated, hardIronOffset));
}

#endif

//------------------------------------------------------------------------------
// End of file
