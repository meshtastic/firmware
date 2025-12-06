/**
 * @file FusionAxes.h
 * @author Seb Madgwick
 * @brief Swaps sensor axes for alignment with the body axes.
 */

#ifndef FUSION_AXES_H
#define FUSION_AXES_H

//------------------------------------------------------------------------------
// Includes

#include "FusionMath.h"

//------------------------------------------------------------------------------
// Definitions

/**
 * @brief Axes alignment describing the sensor axes relative to the body axes.
 * For example, if the body X axis is aligned with the sensor Y axis and the
 * body Y axis is aligned with sensor X axis but pointing the opposite direction
 * then alignment is +Y-X+Z.
 */
typedef enum {
    FusionAxesAlignmentPXPYPZ, /* +X+Y+Z */
    FusionAxesAlignmentPXNZPY, /* +X-Z+Y */
    FusionAxesAlignmentPXNYNZ, /* +X-Y-Z */
    FusionAxesAlignmentPXPZNY, /* +X+Z-Y */
    FusionAxesAlignmentNXPYNZ, /* -X+Y-Z */
    FusionAxesAlignmentNXPZPY, /* -X+Z+Y */
    FusionAxesAlignmentNXNYPZ, /* -X-Y+Z */
    FusionAxesAlignmentNXNZNY, /* -X-Z-Y */
    FusionAxesAlignmentPYNXPZ, /* +Y-X+Z */
    FusionAxesAlignmentPYNZNX, /* +Y-Z-X */
    FusionAxesAlignmentPYPXNZ, /* +Y+X-Z */
    FusionAxesAlignmentPYPZPX, /* +Y+Z+X */
    FusionAxesAlignmentNYPXPZ, /* -Y+X+Z */
    FusionAxesAlignmentNYNZPX, /* -Y-Z+X */
    FusionAxesAlignmentNYNXNZ, /* -Y-X-Z */
    FusionAxesAlignmentNYPZNX, /* -Y+Z-X */
    FusionAxesAlignmentPZPYNX, /* +Z+Y-X */
    FusionAxesAlignmentPZPXPY, /* +Z+X+Y */
    FusionAxesAlignmentPZNYPX, /* +Z-Y+X */
    FusionAxesAlignmentPZNXNY, /* +Z-X-Y */
    FusionAxesAlignmentNZPYPX, /* -Z+Y+X */
    FusionAxesAlignmentNZNXPY, /* -Z-X+Y */
    FusionAxesAlignmentNZNYNX, /* -Z-Y-X */
    FusionAxesAlignmentNZPXNY, /* -Z+X-Y */
} FusionAxesAlignment;

//------------------------------------------------------------------------------
// Inline functions

/**
 * @brief Swaps sensor axes for alignment with the body axes.
 * @param sensor Sensor axes.
 * @param alignment Axes alignment.
 * @return Sensor axes aligned with the body axes.
 */
static inline FusionVector FusionAxesSwap(const FusionVector sensor, const FusionAxesAlignment alignment)
{
    FusionVector result;
    switch (alignment) {
    case FusionAxesAlignmentPXPYPZ:
        break;
    case FusionAxesAlignmentPXNZPY:
        result.axis.x = +sensor.axis.x;
        result.axis.y = -sensor.axis.z;
        result.axis.z = +sensor.axis.y;
        return result;
    case FusionAxesAlignmentPXNYNZ:
        result.axis.x = +sensor.axis.x;
        result.axis.y = -sensor.axis.y;
        result.axis.z = -sensor.axis.z;
        return result;
    case FusionAxesAlignmentPXPZNY:
        result.axis.x = +sensor.axis.x;
        result.axis.y = +sensor.axis.z;
        result.axis.z = -sensor.axis.y;
        return result;
    case FusionAxesAlignmentNXPYNZ:
        result.axis.x = -sensor.axis.x;
        result.axis.y = +sensor.axis.y;
        result.axis.z = -sensor.axis.z;
        return result;
    case FusionAxesAlignmentNXPZPY:
        result.axis.x = -sensor.axis.x;
        result.axis.y = +sensor.axis.z;
        result.axis.z = +sensor.axis.y;
        return result;
    case FusionAxesAlignmentNXNYPZ:
        result.axis.x = -sensor.axis.x;
        result.axis.y = -sensor.axis.y;
        result.axis.z = +sensor.axis.z;
        return result;
    case FusionAxesAlignmentNXNZNY:
        result.axis.x = -sensor.axis.x;
        result.axis.y = -sensor.axis.z;
        result.axis.z = -sensor.axis.y;
        return result;
    case FusionAxesAlignmentPYNXPZ:
        result.axis.x = +sensor.axis.y;
        result.axis.y = -sensor.axis.x;
        result.axis.z = +sensor.axis.z;
        return result;
    case FusionAxesAlignmentPYNZNX:
        result.axis.x = +sensor.axis.y;
        result.axis.y = -sensor.axis.z;
        result.axis.z = -sensor.axis.x;
        return result;
    case FusionAxesAlignmentPYPXNZ:
        result.axis.x = +sensor.axis.y;
        result.axis.y = +sensor.axis.x;
        result.axis.z = -sensor.axis.z;
        return result;
    case FusionAxesAlignmentPYPZPX:
        result.axis.x = +sensor.axis.y;
        result.axis.y = +sensor.axis.z;
        result.axis.z = +sensor.axis.x;
        return result;
    case FusionAxesAlignmentNYPXPZ:
        result.axis.x = -sensor.axis.y;
        result.axis.y = +sensor.axis.x;
        result.axis.z = +sensor.axis.z;
        return result;
    case FusionAxesAlignmentNYNZPX:
        result.axis.x = -sensor.axis.y;
        result.axis.y = -sensor.axis.z;
        result.axis.z = +sensor.axis.x;
        return result;
    case FusionAxesAlignmentNYNXNZ:
        result.axis.x = -sensor.axis.y;
        result.axis.y = -sensor.axis.x;
        result.axis.z = -sensor.axis.z;
        return result;
    case FusionAxesAlignmentNYPZNX:
        result.axis.x = -sensor.axis.y;
        result.axis.y = +sensor.axis.z;
        result.axis.z = -sensor.axis.x;
        return result;
    case FusionAxesAlignmentPZPYNX:
        result.axis.x = +sensor.axis.z;
        result.axis.y = +sensor.axis.y;
        result.axis.z = -sensor.axis.x;
        return result;
    case FusionAxesAlignmentPZPXPY:
        result.axis.x = +sensor.axis.z;
        result.axis.y = +sensor.axis.x;
        result.axis.z = +sensor.axis.y;
        return result;
    case FusionAxesAlignmentPZNYPX:
        result.axis.x = +sensor.axis.z;
        result.axis.y = -sensor.axis.y;
        result.axis.z = +sensor.axis.x;
        return result;
    case FusionAxesAlignmentPZNXNY:
        result.axis.x = +sensor.axis.z;
        result.axis.y = -sensor.axis.x;
        result.axis.z = -sensor.axis.y;
        return result;
    case FusionAxesAlignmentNZPYPX:
        result.axis.x = -sensor.axis.z;
        result.axis.y = +sensor.axis.y;
        result.axis.z = +sensor.axis.x;
        return result;
    case FusionAxesAlignmentNZNXPY:
        result.axis.x = -sensor.axis.z;
        result.axis.y = -sensor.axis.x;
        result.axis.z = +sensor.axis.y;
        return result;
    case FusionAxesAlignmentNZNYNX:
        result.axis.x = -sensor.axis.z;
        result.axis.y = -sensor.axis.y;
        result.axis.z = -sensor.axis.x;
        return result;
    case FusionAxesAlignmentNZPXNY:
        result.axis.x = -sensor.axis.z;
        result.axis.y = +sensor.axis.x;
        result.axis.z = -sensor.axis.y;
        return result;
    }
    return sensor; // avoid compiler warning
}

#endif

//------------------------------------------------------------------------------
// End of file
