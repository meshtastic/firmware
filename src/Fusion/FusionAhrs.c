/**
 * @file FusionAhrs.c
 * @author Seb Madgwick
 * @brief AHRS algorithm to combine gyroscope, accelerometer, and magnetometer
 * measurements into a single measurement of orientation relative to the Earth.
 */

//------------------------------------------------------------------------------
// Includes

#include "FusionAhrs.h"
#include <float.h> // FLT_MAX
#include <math.h>  // atan2f, cosf, fabsf, powf, sinf

//------------------------------------------------------------------------------
// Definitions

/**
 * @brief Initial gain used during the initialisation.
 */
#define INITIAL_GAIN (10.0f)

/**
 * @brief Initialisation period in seconds.
 */
#define INITIALISATION_PERIOD (3.0f)

//------------------------------------------------------------------------------
// Function declarations

static inline FusionVector HalfGravity(const FusionAhrs *const ahrs);

static inline FusionVector HalfMagnetic(const FusionAhrs *const ahrs);

static inline FusionVector Feedback(const FusionVector sensor, const FusionVector reference);

static inline int Clamp(const int value, const int min, const int max);

//------------------------------------------------------------------------------
// Functions

/**
 * @brief Initialises the AHRS algorithm structure.
 * @param ahrs AHRS algorithm structure.
 */
void FusionAhrsInitialise(FusionAhrs *const ahrs)
{
    const FusionAhrsSettings settings = {
        .convention = FusionConventionNwu,
        .gain = 0.5f,
        .gyroscopeRange = 0.0f,
        .accelerationRejection = 90.0f,
        .magneticRejection = 90.0f,
        .recoveryTriggerPeriod = 0,
    };
    FusionAhrsSetSettings(ahrs, &settings);
    FusionAhrsReset(ahrs);
}

/**
 * @brief Resets the AHRS algorithm.  This is equivalent to reinitialising the
 * algorithm while maintaining the current settings.
 * @param ahrs AHRS algorithm structure.
 */
void FusionAhrsReset(FusionAhrs *const ahrs)
{
    ahrs->quaternion = FUSION_IDENTITY_QUATERNION;
    ahrs->accelerometer = FUSION_VECTOR_ZERO;
    ahrs->initialising = true;
    ahrs->rampedGain = INITIAL_GAIN;
    ahrs->angularRateRecovery = false;
    ahrs->halfAccelerometerFeedback = FUSION_VECTOR_ZERO;
    ahrs->halfMagnetometerFeedback = FUSION_VECTOR_ZERO;
    ahrs->accelerometerIgnored = false;
    ahrs->accelerationRecoveryTrigger = 0;
    ahrs->accelerationRecoveryTimeout = ahrs->settings.recoveryTriggerPeriod;
    ahrs->magnetometerIgnored = false;
    ahrs->magneticRecoveryTrigger = 0;
    ahrs->magneticRecoveryTimeout = ahrs->settings.recoveryTriggerPeriod;
}

/**
 * @brief Sets the AHRS algorithm settings.
 * @param ahrs AHRS algorithm structure.
 * @param settings Settings.
 */
void FusionAhrsSetSettings(FusionAhrs *const ahrs, const FusionAhrsSettings *const settings)
{
    ahrs->settings.convention = settings->convention;
    ahrs->settings.gain = settings->gain;
    ahrs->settings.gyroscopeRange = settings->gyroscopeRange == 0.0f ? FLT_MAX : 0.98f * settings->gyroscopeRange;
    ahrs->settings.accelerationRejection = settings->accelerationRejection == 0.0f
                                               ? FLT_MAX
                                               : powf(0.5f * sinf(FusionDegreesToRadians(settings->accelerationRejection)), 2);
    ahrs->settings.magneticRejection =
        settings->magneticRejection == 0.0f ? FLT_MAX : powf(0.5f * sinf(FusionDegreesToRadians(settings->magneticRejection)), 2);
    ahrs->settings.recoveryTriggerPeriod = settings->recoveryTriggerPeriod;
    ahrs->accelerationRecoveryTimeout = ahrs->settings.recoveryTriggerPeriod;
    ahrs->magneticRecoveryTimeout = ahrs->settings.recoveryTriggerPeriod;
    if ((settings->gain == 0.0f) ||
        (settings->recoveryTriggerPeriod == 0)) { // disable acceleration and magnetic rejection features if gain is zero
        ahrs->settings.accelerationRejection = FLT_MAX;
        ahrs->settings.magneticRejection = FLT_MAX;
    }
    if (ahrs->initialising == false) {
        ahrs->rampedGain = ahrs->settings.gain;
    }
    ahrs->rampedGainStep = (INITIAL_GAIN - ahrs->settings.gain) / INITIALISATION_PERIOD;
}

/**
 * @brief Updates the AHRS algorithm using the gyroscope, accelerometer, and
 * magnetometer measurements.
 * @param ahrs AHRS algorithm structure.
 * @param gyroscope Gyroscope measurement in degrees per second.
 * @param accelerometer Accelerometer measurement in g.
 * @param magnetometer Magnetometer measurement in arbitrary units.
 * @param deltaTime Delta time in seconds.
 */
void FusionAhrsUpdate(FusionAhrs *const ahrs, const FusionVector gyroscope, const FusionVector accelerometer,
                      const FusionVector magnetometer, const float deltaTime)
{
#define Q ahrs->quaternion.element

    // Store accelerometer
    ahrs->accelerometer = accelerometer;

    // Reinitialise if gyroscope range exceeded
    if ((fabsf(gyroscope.axis.x) > ahrs->settings.gyroscopeRange) || (fabsf(gyroscope.axis.y) > ahrs->settings.gyroscopeRange) ||
        (fabsf(gyroscope.axis.z) > ahrs->settings.gyroscopeRange)) {
        const FusionQuaternion quaternion = ahrs->quaternion;
        FusionAhrsReset(ahrs);
        ahrs->quaternion = quaternion;
        ahrs->angularRateRecovery = true;
    }

    // Ramp down gain during initialisation
    if (ahrs->initialising) {
        ahrs->rampedGain -= ahrs->rampedGainStep * deltaTime;
        if ((ahrs->rampedGain < ahrs->settings.gain) || (ahrs->settings.gain == 0.0f)) {
            ahrs->rampedGain = ahrs->settings.gain;
            ahrs->initialising = false;
            ahrs->angularRateRecovery = false;
        }
    }

    // Calculate direction of gravity indicated by algorithm
    const FusionVector halfGravity = HalfGravity(ahrs);

    // Calculate accelerometer feedback
    FusionVector halfAccelerometerFeedback = FUSION_VECTOR_ZERO;
    ahrs->accelerometerIgnored = true;
    if (FusionVectorIsZero(accelerometer) == false) {

        // Calculate accelerometer feedback scaled by 0.5
        ahrs->halfAccelerometerFeedback = Feedback(FusionVectorNormalise(accelerometer), halfGravity);

        // Don't ignore accelerometer if acceleration error below threshold
        if (ahrs->initialising ||
            ((FusionVectorMagnitudeSquared(ahrs->halfAccelerometerFeedback) <= ahrs->settings.accelerationRejection))) {
            ahrs->accelerometerIgnored = false;
            ahrs->accelerationRecoveryTrigger -= 9;
        } else {
            ahrs->accelerationRecoveryTrigger += 1;
        }

        // Don't ignore accelerometer during acceleration recovery
        if (ahrs->accelerationRecoveryTrigger > ahrs->accelerationRecoveryTimeout) {
            ahrs->accelerationRecoveryTimeout = 0;
            ahrs->accelerometerIgnored = false;
        } else {
            ahrs->accelerationRecoveryTimeout = ahrs->settings.recoveryTriggerPeriod;
        }
        ahrs->accelerationRecoveryTrigger = Clamp(ahrs->accelerationRecoveryTrigger, 0, ahrs->settings.recoveryTriggerPeriod);

        // Apply accelerometer feedback
        if (ahrs->accelerometerIgnored == false) {
            halfAccelerometerFeedback = ahrs->halfAccelerometerFeedback;
        }
    }

    // Calculate magnetometer feedback
    FusionVector halfMagnetometerFeedback = FUSION_VECTOR_ZERO;
    ahrs->magnetometerIgnored = true;
    if (FusionVectorIsZero(magnetometer) == false) {

        // Calculate direction of magnetic field indicated by algorithm
        const FusionVector halfMagnetic = HalfMagnetic(ahrs);

        // Calculate magnetometer feedback scaled by 0.5
        ahrs->halfMagnetometerFeedback =
            Feedback(FusionVectorNormalise(FusionVectorCrossProduct(halfGravity, magnetometer)), halfMagnetic);

        // Don't ignore magnetometer if magnetic error below threshold
        if (ahrs->initialising ||
            ((FusionVectorMagnitudeSquared(ahrs->halfMagnetometerFeedback) <= ahrs->settings.magneticRejection))) {
            ahrs->magnetometerIgnored = false;
            ahrs->magneticRecoveryTrigger -= 9;
        } else {
            ahrs->magneticRecoveryTrigger += 1;
        }

        // Don't ignore magnetometer during magnetic recovery
        if (ahrs->magneticRecoveryTrigger > ahrs->magneticRecoveryTimeout) {
            ahrs->magneticRecoveryTimeout = 0;
            ahrs->magnetometerIgnored = false;
        } else {
            ahrs->magneticRecoveryTimeout = ahrs->settings.recoveryTriggerPeriod;
        }
        ahrs->magneticRecoveryTrigger = Clamp(ahrs->magneticRecoveryTrigger, 0, ahrs->settings.recoveryTriggerPeriod);

        // Apply magnetometer feedback
        if (ahrs->magnetometerIgnored == false) {
            halfMagnetometerFeedback = ahrs->halfMagnetometerFeedback;
        }
    }

    // Convert gyroscope to radians per second scaled by 0.5
    const FusionVector halfGyroscope = FusionVectorMultiplyScalar(gyroscope, FusionDegreesToRadians(0.5f));

    // Apply feedback to gyroscope
    const FusionVector adjustedHalfGyroscope = FusionVectorAdd(
        halfGyroscope,
        FusionVectorMultiplyScalar(FusionVectorAdd(halfAccelerometerFeedback, halfMagnetometerFeedback), ahrs->rampedGain));

    // Integrate rate of change of quaternion
    ahrs->quaternion = FusionQuaternionAdd(
        ahrs->quaternion,
        FusionQuaternionMultiplyVector(ahrs->quaternion, FusionVectorMultiplyScalar(adjustedHalfGyroscope, deltaTime)));

    // Normalise quaternion
    ahrs->quaternion = FusionQuaternionNormalise(ahrs->quaternion);
#undef Q
}

/**
 * @brief Returns the direction of gravity scaled by 0.5.
 * @param ahrs AHRS algorithm structure.
 * @return Direction of gravity scaled by 0.5.
 */
static inline FusionVector HalfGravity(const FusionAhrs *const ahrs)
{
#define Q ahrs->quaternion.element
    switch (ahrs->settings.convention) {
    case FusionConventionNwu:
    case FusionConventionEnu: {
        const FusionVector halfGravity = {.axis = {
                                              .x = Q.x * Q.z - Q.w * Q.y,
                                              .y = Q.y * Q.z + Q.w * Q.x,
                                              .z = Q.w * Q.w - 0.5f + Q.z * Q.z,
                                          }}; // third column of transposed rotation matrix scaled by 0.5
        return halfGravity;
    }
    case FusionConventionNed: {
        const FusionVector halfGravity = {.axis = {
                                              .x = Q.w * Q.y - Q.x * Q.z,
                                              .y = -1.0f * (Q.y * Q.z + Q.w * Q.x),
                                              .z = 0.5f - Q.w * Q.w - Q.z * Q.z,
                                          }}; // third column of transposed rotation matrix scaled by -0.5
        return halfGravity;
    }
    }
    return FUSION_VECTOR_ZERO; // avoid compiler warning
#undef Q
}

/**
 * @brief Returns the direction of the magnetic field scaled by 0.5.
 * @param ahrs AHRS algorithm structure.
 * @return Direction of the magnetic field scaled by 0.5.
 */
static inline FusionVector HalfMagnetic(const FusionAhrs *const ahrs)
{
#define Q ahrs->quaternion.element
    switch (ahrs->settings.convention) {
    case FusionConventionNwu: {
        const FusionVector halfMagnetic = {.axis = {
                                               .x = Q.x * Q.y + Q.w * Q.z,
                                               .y = Q.w * Q.w - 0.5f + Q.y * Q.y,
                                               .z = Q.y * Q.z - Q.w * Q.x,
                                           }}; // second column of transposed rotation matrix scaled by 0.5
        return halfMagnetic;
    }
    case FusionConventionEnu: {
        const FusionVector halfMagnetic = {.axis = {
                                               .x = 0.5f - Q.w * Q.w - Q.x * Q.x,
                                               .y = Q.w * Q.z - Q.x * Q.y,
                                               .z = -1.0f * (Q.x * Q.z + Q.w * Q.y),
                                           }}; // first column of transposed rotation matrix scaled by -0.5
        return halfMagnetic;
    }
    case FusionConventionNed: {
        const FusionVector halfMagnetic = {.axis = {
                                               .x = -1.0f * (Q.x * Q.y + Q.w * Q.z),
                                               .y = 0.5f - Q.w * Q.w - Q.y * Q.y,
                                               .z = Q.w * Q.x - Q.y * Q.z,
                                           }}; // second column of transposed rotation matrix scaled by -0.5
        return halfMagnetic;
    }
    }
    return FUSION_VECTOR_ZERO; // avoid compiler warning
#undef Q
}

/**
 * @brief Returns the feedback.
 * @param sensor Sensor.
 * @param reference Reference.
 * @return Feedback.
 */
static inline FusionVector Feedback(const FusionVector sensor, const FusionVector reference)
{
    if (FusionVectorDotProduct(sensor, reference) < 0.0f) { // if error is >90 degrees
        return FusionVectorNormalise(FusionVectorCrossProduct(sensor, reference));
    }
    return FusionVectorCrossProduct(sensor, reference);
}

/**
 * @brief Returns a value limited to maximum and minimum.
 * @param value Value.
 * @param min Minimum value.
 * @param max Maximum value.
 * @return Value limited to maximum and minimum.
 */
static inline int Clamp(const int value, const int min, const int max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

/**
 * @brief Updates the AHRS algorithm using the gyroscope and accelerometer
 * measurements only.
 * @param ahrs AHRS algorithm structure.
 * @param gyroscope Gyroscope measurement in degrees per second.
 * @param accelerometer Accelerometer measurement in g.
 * @param deltaTime Delta time in seconds.
 */
void FusionAhrsUpdateNoMagnetometer(FusionAhrs *const ahrs, const FusionVector gyroscope, const FusionVector accelerometer,
                                    const float deltaTime)
{

    // Update AHRS algorithm
    FusionAhrsUpdate(ahrs, gyroscope, accelerometer, FUSION_VECTOR_ZERO, deltaTime);

    // Zero heading during initialisation
    if (ahrs->initialising) {
        FusionAhrsSetHeading(ahrs, 0.0f);
    }
}

/**
 * @brief Updates the AHRS algorithm using the gyroscope, accelerometer, and
 * heading measurements.
 * @param ahrs AHRS algorithm structure.
 * @param gyroscope Gyroscope measurement in degrees per second.
 * @param accelerometer Accelerometer measurement in g.
 * @param heading Heading measurement in degrees.
 * @param deltaTime Delta time in seconds.
 */
void FusionAhrsUpdateExternalHeading(FusionAhrs *const ahrs, const FusionVector gyroscope, const FusionVector accelerometer,
                                     const float heading, const float deltaTime)
{
#define Q ahrs->quaternion.element

    // Calculate roll
    const float roll = atan2f(Q.w * Q.x + Q.y * Q.z, 0.5f - Q.y * Q.y - Q.x * Q.x);

    // Calculate magnetometer
    const float headingRadians = FusionDegreesToRadians(heading);
    const float sinHeadingRadians = sinf(headingRadians);
    const FusionVector magnetometer = {.axis = {
                                           .x = cosf(headingRadians),
                                           .y = -1.0f * cosf(roll) * sinHeadingRadians,
                                           .z = sinHeadingRadians * sinf(roll),
                                       }};

    // Update AHRS algorithm
    FusionAhrsUpdate(ahrs, gyroscope, accelerometer, magnetometer, deltaTime);
#undef Q
}

/**
 * @brief Returns the quaternion describing the sensor relative to the Earth.
 * @param ahrs AHRS algorithm structure.
 * @return Quaternion describing the sensor relative to the Earth.
 */
FusionQuaternion FusionAhrsGetQuaternion(const FusionAhrs *const ahrs)
{
    return ahrs->quaternion;
}

/**
 * @brief Sets the quaternion describing the sensor relative to the Earth.
 * @param ahrs AHRS algorithm structure.
 * @param quaternion Quaternion describing the sensor relative to the Earth.
 */
void FusionAhrsSetQuaternion(FusionAhrs *const ahrs, const FusionQuaternion quaternion)
{
    ahrs->quaternion = quaternion;
}

/**
 * @brief Returns the linear acceleration measurement equal to the accelerometer
 * measurement with the 1 g of gravity removed.
 * @param ahrs AHRS algorithm structure.
 * @return Linear acceleration measurement in g.
 */
FusionVector FusionAhrsGetLinearAcceleration(const FusionAhrs *const ahrs)
{
#define Q ahrs->quaternion.element

    // Calculate gravity in the sensor coordinate frame
    const FusionVector gravity = {.axis = {
                                      .x = 2.0f * (Q.x * Q.z - Q.w * Q.y),
                                      .y = 2.0f * (Q.y * Q.z + Q.w * Q.x),
                                      .z = 2.0f * (Q.w * Q.w - 0.5f + Q.z * Q.z),
                                  }}; // third column of transposed rotation matrix

    // Remove gravity from accelerometer measurement
    switch (ahrs->settings.convention) {
    case FusionConventionNwu:
    case FusionConventionEnu: {
        return FusionVectorSubtract(ahrs->accelerometer, gravity);
    }
    case FusionConventionNed: {
        return FusionVectorAdd(ahrs->accelerometer, gravity);
    }
    }
    return FUSION_VECTOR_ZERO; // avoid compiler warning
#undef Q
}

/**
 * @brief Returns the Earth acceleration measurement equal to accelerometer
 * measurement in the Earth coordinate frame with the 1 g of gravity removed.
 * @param ahrs AHRS algorithm structure.
 * @return Earth acceleration measurement in g.
 */
FusionVector FusionAhrsGetEarthAcceleration(const FusionAhrs *const ahrs)
{
#define Q ahrs->quaternion.element
#define A ahrs->accelerometer.axis

    // Calculate accelerometer measurement in the Earth coordinate frame
    const float qwqw = Q.w * Q.w; // calculate common terms to avoid repeated operations
    const float qwqx = Q.w * Q.x;
    const float qwqy = Q.w * Q.y;
    const float qwqz = Q.w * Q.z;
    const float qxqy = Q.x * Q.y;
    const float qxqz = Q.x * Q.z;
    const float qyqz = Q.y * Q.z;
    FusionVector accelerometer = {.axis = {
                                      .x = 2.0f * ((qwqw - 0.5f + Q.x * Q.x) * A.x + (qxqy - qwqz) * A.y + (qxqz + qwqy) * A.z),
                                      .y = 2.0f * ((qxqy + qwqz) * A.x + (qwqw - 0.5f + Q.y * Q.y) * A.y + (qyqz - qwqx) * A.z),
                                      .z = 2.0f * ((qxqz - qwqy) * A.x + (qyqz + qwqx) * A.y + (qwqw - 0.5f + Q.z * Q.z) * A.z),
                                  }}; // rotation matrix multiplied with the accelerometer

    // Remove gravity from accelerometer measurement
    switch (ahrs->settings.convention) {
    case FusionConventionNwu:
    case FusionConventionEnu:
        accelerometer.axis.z -= 1.0f;
        break;
    case FusionConventionNed:
        accelerometer.axis.z += 1.0f;
        break;
    }
    return accelerometer;
#undef Q
#undef A
}

/**
 * @brief Returns the AHRS algorithm internal states.
 * @param ahrs AHRS algorithm structure.
 * @return AHRS algorithm internal states.
 */
FusionAhrsInternalStates FusionAhrsGetInternalStates(const FusionAhrs *const ahrs)
{
    const FusionAhrsInternalStates internalStates = {
        .accelerationError = FusionRadiansToDegrees(FusionAsin(2.0f * FusionVectorMagnitude(ahrs->halfAccelerometerFeedback))),
        .accelerometerIgnored = ahrs->accelerometerIgnored,
        .accelerationRecoveryTrigger =
            ahrs->settings.recoveryTriggerPeriod == 0
                ? 0.0f
                : (float)ahrs->accelerationRecoveryTrigger / (float)ahrs->settings.recoveryTriggerPeriod,
        .magneticError = FusionRadiansToDegrees(FusionAsin(2.0f * FusionVectorMagnitude(ahrs->halfMagnetometerFeedback))),
        .magnetometerIgnored = ahrs->magnetometerIgnored,
        .magneticRecoveryTrigger = ahrs->settings.recoveryTriggerPeriod == 0
                                       ? 0.0f
                                       : (float)ahrs->magneticRecoveryTrigger / (float)ahrs->settings.recoveryTriggerPeriod,
    };
    return internalStates;
}

/**
 * @brief Returns the AHRS algorithm flags.
 * @param ahrs AHRS algorithm structure.
 * @return AHRS algorithm flags.
 */
FusionAhrsFlags FusionAhrsGetFlags(const FusionAhrs *const ahrs)
{
    const FusionAhrsFlags flags = {
        .initialising = ahrs->initialising,
        .angularRateRecovery = ahrs->angularRateRecovery,
        .accelerationRecovery = ahrs->accelerationRecoveryTrigger > ahrs->accelerationRecoveryTimeout,
        .magneticRecovery = ahrs->magneticRecoveryTrigger > ahrs->magneticRecoveryTimeout,
    };
    return flags;
}

/**
 * @brief Sets the heading of the orientation measurement provided by the AHRS
 * algorithm.  This function can be used to reset drift in heading when the AHRS
 * algorithm is being used without a magnetometer.
 * @param ahrs AHRS algorithm structure.
 * @param heading Heading angle in degrees.
 */
void FusionAhrsSetHeading(FusionAhrs *const ahrs, const float heading)
{
#define Q ahrs->quaternion.element
    const float yaw = atan2f(Q.w * Q.z + Q.x * Q.y, 0.5f - Q.y * Q.y - Q.z * Q.z);
    const float halfYawMinusHeading = 0.5f * (yaw - FusionDegreesToRadians(heading));
    const FusionQuaternion rotation = {.element = {
                                           .w = cosf(halfYawMinusHeading),
                                           .x = 0.0f,
                                           .y = 0.0f,
                                           .z = -1.0f * sinf(halfYawMinusHeading),
                                       }};
    ahrs->quaternion = FusionQuaternionMultiply(rotation, ahrs->quaternion);
#undef Q
}

//------------------------------------------------------------------------------
// End of file
