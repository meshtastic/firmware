/**
 * @file FusionCompass.c
 * @author Seb Madgwick
 * @brief Tilt-compensated compass to calculate the magnetic heading using
 * accelerometer and magnetometer measurements.
 */

//------------------------------------------------------------------------------
// Includes

#include "FusionCompass.h"
#include "FusionAxes.h"
#include <math.h> // atan2f

//------------------------------------------------------------------------------
// Functions

/**
 * @brief Calculates the magnetic heading.
 * @param convention Earth axes convention.
 * @param accelerometer Accelerometer measurement in any calibrated units.
 * @param magnetometer Magnetometer measurement in any calibrated units.
 * @return Heading angle in degrees.
 */
float FusionCompassCalculateHeading(const FusionConvention convention, const FusionVector accelerometer,
                                    const FusionVector magnetometer)
{
    switch (convention) {
    case FusionConventionNwu: {
        const FusionVector west = FusionVectorNormalise(FusionVectorCrossProduct(accelerometer, magnetometer));
        const FusionVector north = FusionVectorNormalise(FusionVectorCrossProduct(west, accelerometer));
        return FusionRadiansToDegrees(atan2f(west.axis.x, north.axis.x));
    }
    case FusionConventionEnu: {
        const FusionVector west = FusionVectorNormalise(FusionVectorCrossProduct(accelerometer, magnetometer));
        const FusionVector north = FusionVectorNormalise(FusionVectorCrossProduct(west, accelerometer));
        const FusionVector east = FusionVectorMultiplyScalar(west, -1.0f);
        return FusionRadiansToDegrees(atan2f(north.axis.x, east.axis.x));
    }
    case FusionConventionNed: {
        const FusionVector up = FusionVectorMultiplyScalar(accelerometer, -1.0f);
        const FusionVector west = FusionVectorNormalise(FusionVectorCrossProduct(up, magnetometer));
        const FusionVector north = FusionVectorNormalise(FusionVectorCrossProduct(west, up));
        return FusionRadiansToDegrees(atan2f(west.axis.x, north.axis.x));
    }
    }
    return 0; // avoid compiler warning
}

//------------------------------------------------------------------------------
// End of file
