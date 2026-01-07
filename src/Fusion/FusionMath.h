/**
 * @file FusionMath.h
 * @author Seb Madgwick
 * @brief Math library.
 */

#ifndef FUSION_MATH_H
#define FUSION_MATH_H

//------------------------------------------------------------------------------
// Includes

#include <math.h> // M_PI, sqrtf, atan2f, asinf
#include <stdbool.h>
#include <stdint.h>

//------------------------------------------------------------------------------
// Definitions

/**
 * @brief 3D vector.
 */
typedef union {
    float array[3];

    struct {
        float x;
        float y;
        float z;
    } axis;
} FusionVector;

/**
 * @brief Quaternion.
 */
typedef union {
    float array[4];

    struct {
        float w;
        float x;
        float y;
        float z;
    } element;
} FusionQuaternion;

/**
 * @brief 3x3 matrix in row-major order.
 * See http://en.wikipedia.org/wiki/Row-major_order
 */
typedef union {
    float array[3][3];

    struct {
        float xx;
        float xy;
        float xz;
        float yx;
        float yy;
        float yz;
        float zx;
        float zy;
        float zz;
    } element;
} FusionMatrix;

/**
 * @brief Euler angles.  Roll, pitch, and yaw correspond to rotations around
 * X, Y, and Z respectively.
 */
typedef union {
    float array[3];

    struct {
        float roll;
        float pitch;
        float yaw;
    } angle;
} FusionEuler;

/**
 * @brief Vector of zeros.
 */
#define FUSION_VECTOR_ZERO ((FusionVector){.array = {0.0f, 0.0f, 0.0f}})

/**
 * @brief Vector of ones.
 */
#define FUSION_VECTOR_ONES ((FusionVector){.array = {1.0f, 1.0f, 1.0f}})

/**
 * @brief Identity quaternion.
 */
#define FUSION_IDENTITY_QUATERNION ((FusionQuaternion){.array = {1.0f, 0.0f, 0.0f, 0.0f}})

/**
 * @brief Identity matrix.
 */
#define FUSION_IDENTITY_MATRIX ((FusionMatrix){.array = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}})

/**
 * @brief Euler angles of zero.
 */
#define FUSION_EULER_ZERO ((FusionEuler){.array = {0.0f, 0.0f, 0.0f}})

/**
 * @brief Pi. May not be defined in math.h.
 */
#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

/**
 * @brief Include this definition or add as a preprocessor definition to use
 * normal square root operations.
 */
// #define FUSION_USE_NORMAL_SQRT

//------------------------------------------------------------------------------
// Inline functions - Degrees and radians conversion

/**
 * @brief Converts degrees to radians.
 * @param degrees Degrees.
 * @return Radians.
 */
static inline float FusionDegreesToRadians(const float degrees)
{
    return degrees * ((float)M_PI / 180.0f);
}

/**
 * @brief Converts radians to degrees.
 * @param radians Radians.
 * @return Degrees.
 */
static inline float FusionRadiansToDegrees(const float radians)
{
    return radians * (180.0f / (float)M_PI);
}

//------------------------------------------------------------------------------
// Inline functions - Arc sine

/**
 * @brief Returns the arc sine of the value.
 * @param value Value.
 * @return Arc sine of the value.
 */
static inline float FusionAsin(const float value)
{
    if (value <= -1.0f) {
        return (float)M_PI / -2.0f;
    }
    if (value >= 1.0f) {
        return (float)M_PI / 2.0f;
    }
    return asinf(value);
}

//------------------------------------------------------------------------------
// Inline functions - Fast inverse square root

#ifndef FUSION_USE_NORMAL_SQRT

/**
 * @brief Calculates the reciprocal of the square root.
 * See https://pizer.wordpress.com/2008/10/12/fast-inverse-square-root/
 * @param x Operand.
 * @return Reciprocal of the square root of x.
 */
static inline float FusionFastInverseSqrt(const float x)
{

    typedef union {
        float f;
        int32_t i;
    } Union32;

    Union32 union32 = {.f = x};
    union32.i = 0x5F1F1412 - (union32.i >> 1);
    return union32.f * (1.69000231f - 0.714158168f * x * union32.f * union32.f);
}

#endif

//------------------------------------------------------------------------------
// Inline functions - Vector operations

/**
 * @brief Returns true if the vector is zero.
 * @param vector Vector.
 * @return True if the vector is zero.
 */
static inline bool FusionVectorIsZero(const FusionVector vector)
{
    return (vector.axis.x == 0.0f) && (vector.axis.y == 0.0f) && (vector.axis.z == 0.0f);
}

/**
 * @brief Returns the sum of two vectors.
 * @param vectorA Vector A.
 * @param vectorB Vector B.
 * @return Sum of two vectors.
 */
static inline FusionVector FusionVectorAdd(const FusionVector vectorA, const FusionVector vectorB)
{
    const FusionVector result = {.axis = {
                                     .x = vectorA.axis.x + vectorB.axis.x,
                                     .y = vectorA.axis.y + vectorB.axis.y,
                                     .z = vectorA.axis.z + vectorB.axis.z,
                                 }};
    return result;
}

/**
 * @brief Returns vector B subtracted from vector A.
 * @param vectorA Vector A.
 * @param vectorB Vector B.
 * @return Vector B subtracted from vector A.
 */
static inline FusionVector FusionVectorSubtract(const FusionVector vectorA, const FusionVector vectorB)
{
    const FusionVector result = {.axis = {
                                     .x = vectorA.axis.x - vectorB.axis.x,
                                     .y = vectorA.axis.y - vectorB.axis.y,
                                     .z = vectorA.axis.z - vectorB.axis.z,
                                 }};
    return result;
}

/**
 * @brief Returns the sum of the elements.
 * @param vector Vector.
 * @return Sum of the elements.
 */
static inline float FusionVectorSum(const FusionVector vector)
{
    return vector.axis.x + vector.axis.y + vector.axis.z;
}

/**
 * @brief Returns the multiplication of a vector by a scalar.
 * @param vector Vector.
 * @param scalar Scalar.
 * @return Multiplication of a vector by a scalar.
 */
static inline FusionVector FusionVectorMultiplyScalar(const FusionVector vector, const float scalar)
{
    const FusionVector result = {.axis = {
                                     .x = vector.axis.x * scalar,
                                     .y = vector.axis.y * scalar,
                                     .z = vector.axis.z * scalar,
                                 }};
    return result;
}

/**
 * @brief Calculates the Hadamard product (element-wise multiplication).
 * @param vectorA Vector A.
 * @param vectorB Vector B.
 * @return Hadamard product.
 */
static inline FusionVector FusionVectorHadamardProduct(const FusionVector vectorA, const FusionVector vectorB)
{
    const FusionVector result = {.axis = {
                                     .x = vectorA.axis.x * vectorB.axis.x,
                                     .y = vectorA.axis.y * vectorB.axis.y,
                                     .z = vectorA.axis.z * vectorB.axis.z,
                                 }};
    return result;
}

/**
 * @brief Returns the cross product.
 * @param vectorA Vector A.
 * @param vectorB Vector B.
 * @return Cross product.
 */
static inline FusionVector FusionVectorCrossProduct(const FusionVector vectorA, const FusionVector vectorB)
{
#define A vectorA.axis
#define B vectorB.axis
    const FusionVector result = {.axis = {
                                     .x = A.y * B.z - A.z * B.y,
                                     .y = A.z * B.x - A.x * B.z,
                                     .z = A.x * B.y - A.y * B.x,
                                 }};
    return result;
#undef A
#undef B
}

/**
 * @brief Returns the dot product.
 * @param vectorA Vector A.
 * @param vectorB Vector B.
 * @return Dot product.
 */
static inline float FusionVectorDotProduct(const FusionVector vectorA, const FusionVector vectorB)
{
    return FusionVectorSum(FusionVectorHadamardProduct(vectorA, vectorB));
}

/**
 * @brief Returns the vector magnitude squared.
 * @param vector Vector.
 * @return Vector magnitude squared.
 */
static inline float FusionVectorMagnitudeSquared(const FusionVector vector)
{
    return FusionVectorSum(FusionVectorHadamardProduct(vector, vector));
}

/**
 * @brief Returns the vector magnitude.
 * @param vector Vector.
 * @return Vector magnitude.
 */
static inline float FusionVectorMagnitude(const FusionVector vector)
{
    return sqrtf(FusionVectorMagnitudeSquared(vector));
}

/**
 * @brief Returns the normalised vector.
 * @param vector Vector.
 * @return Normalised vector.
 */
static inline FusionVector FusionVectorNormalise(const FusionVector vector)
{
#ifdef FUSION_USE_NORMAL_SQRT
    const float magnitudeReciprocal = 1.0f / sqrtf(FusionVectorMagnitudeSquared(vector));
#else
    const float magnitudeReciprocal = FusionFastInverseSqrt(FusionVectorMagnitudeSquared(vector));
#endif
    return FusionVectorMultiplyScalar(vector, magnitudeReciprocal);
}

//------------------------------------------------------------------------------
// Inline functions - Quaternion operations

/**
 * @brief Returns the sum of two quaternions.
 * @param quaternionA Quaternion A.
 * @param quaternionB Quaternion B.
 * @return Sum of two quaternions.
 */
static inline FusionQuaternion FusionQuaternionAdd(const FusionQuaternion quaternionA, const FusionQuaternion quaternionB)
{
    const FusionQuaternion result = {.element = {
                                         .w = quaternionA.element.w + quaternionB.element.w,
                                         .x = quaternionA.element.x + quaternionB.element.x,
                                         .y = quaternionA.element.y + quaternionB.element.y,
                                         .z = quaternionA.element.z + quaternionB.element.z,
                                     }};
    return result;
}

/**
 * @brief Returns the multiplication of two quaternions.
 * @param quaternionA Quaternion A (to be post-multiplied).
 * @param quaternionB Quaternion B (to be pre-multiplied).
 * @return Multiplication of two quaternions.
 */
static inline FusionQuaternion FusionQuaternionMultiply(const FusionQuaternion quaternionA, const FusionQuaternion quaternionB)
{
#define A quaternionA.element
#define B quaternionB.element
    const FusionQuaternion result = {.element = {
                                         .w = A.w * B.w - A.x * B.x - A.y * B.y - A.z * B.z,
                                         .x = A.w * B.x + A.x * B.w + A.y * B.z - A.z * B.y,
                                         .y = A.w * B.y - A.x * B.z + A.y * B.w + A.z * B.x,
                                         .z = A.w * B.z + A.x * B.y - A.y * B.x + A.z * B.w,
                                     }};
    return result;
#undef A
#undef B
}

/**
 * @brief Returns the multiplication of a quaternion with a vector.  This is a
 * normal quaternion multiplication where the vector is treated a
 * quaternion with a W element value of zero.  The quaternion is post-
 * multiplied by the vector.
 * @param quaternion Quaternion.
 * @param vector Vector.
 * @return Multiplication of a quaternion with a vector.
 */
static inline FusionQuaternion FusionQuaternionMultiplyVector(const FusionQuaternion quaternion, const FusionVector vector)
{
#define Q quaternion.element
#define V vector.axis
    const FusionQuaternion result = {.element = {
                                         .w = -Q.x * V.x - Q.y * V.y - Q.z * V.z,
                                         .x = Q.w * V.x + Q.y * V.z - Q.z * V.y,
                                         .y = Q.w * V.y - Q.x * V.z + Q.z * V.x,
                                         .z = Q.w * V.z + Q.x * V.y - Q.y * V.x,
                                     }};
    return result;
#undef Q
#undef V
}

/**
 * @brief Returns the normalised quaternion.
 * @param quaternion Quaternion.
 * @return Normalised quaternion.
 */
static inline FusionQuaternion FusionQuaternionNormalise(const FusionQuaternion quaternion)
{
#define Q quaternion.element
#ifdef FUSION_USE_NORMAL_SQRT
    const float magnitudeReciprocal = 1.0f / sqrtf(Q.w * Q.w + Q.x * Q.x + Q.y * Q.y + Q.z * Q.z);
#else
    const float magnitudeReciprocal = FusionFastInverseSqrt(Q.w * Q.w + Q.x * Q.x + Q.y * Q.y + Q.z * Q.z);
#endif
    const FusionQuaternion result = {.element = {
                                         .w = Q.w * magnitudeReciprocal,
                                         .x = Q.x * magnitudeReciprocal,
                                         .y = Q.y * magnitudeReciprocal,
                                         .z = Q.z * magnitudeReciprocal,
                                     }};
    return result;
#undef Q
}

//------------------------------------------------------------------------------
// Inline functions - Matrix operations

/**
 * @brief Returns the multiplication of a matrix with a vector.
 * @param matrix Matrix.
 * @param vector Vector.
 * @return Multiplication of a matrix with a vector.
 */
static inline FusionVector FusionMatrixMultiplyVector(const FusionMatrix matrix, const FusionVector vector)
{
#define R matrix.element
    const FusionVector result = {.axis = {
                                     .x = R.xx * vector.axis.x + R.xy * vector.axis.y + R.xz * vector.axis.z,
                                     .y = R.yx * vector.axis.x + R.yy * vector.axis.y + R.yz * vector.axis.z,
                                     .z = R.zx * vector.axis.x + R.zy * vector.axis.y + R.zz * vector.axis.z,
                                 }};
    return result;
#undef R
}

//------------------------------------------------------------------------------
// Inline functions - Conversion operations

/**
 * @brief Converts a quaternion to a rotation matrix.
 * @param quaternion Quaternion.
 * @return Rotation matrix.
 */
static inline FusionMatrix FusionQuaternionToMatrix(const FusionQuaternion quaternion)
{
#define Q quaternion.element
    const float qwqw = Q.w * Q.w; // calculate common terms to avoid repeated operations
    const float qwqx = Q.w * Q.x;
    const float qwqy = Q.w * Q.y;
    const float qwqz = Q.w * Q.z;
    const float qxqy = Q.x * Q.y;
    const float qxqz = Q.x * Q.z;
    const float qyqz = Q.y * Q.z;
    const FusionMatrix matrix = {.element = {
                                     .xx = 2.0f * (qwqw - 0.5f + Q.x * Q.x),
                                     .xy = 2.0f * (qxqy - qwqz),
                                     .xz = 2.0f * (qxqz + qwqy),
                                     .yx = 2.0f * (qxqy + qwqz),
                                     .yy = 2.0f * (qwqw - 0.5f + Q.y * Q.y),
                                     .yz = 2.0f * (qyqz - qwqx),
                                     .zx = 2.0f * (qxqz - qwqy),
                                     .zy = 2.0f * (qyqz + qwqx),
                                     .zz = 2.0f * (qwqw - 0.5f + Q.z * Q.z),
                                 }};
    return matrix;
#undef Q
}

/**
 * @brief Converts a quaternion to ZYX Euler angles in degrees.
 * @param quaternion Quaternion.
 * @return Euler angles in degrees.
 */
static inline FusionEuler FusionQuaternionToEuler(const FusionQuaternion quaternion)
{
#define Q quaternion.element
    const float halfMinusQySquared = 0.5f - Q.y * Q.y; // calculate common terms to avoid repeated operations
    const FusionEuler euler = {.angle = {
                                   .roll = FusionRadiansToDegrees(atan2f(Q.w * Q.x + Q.y * Q.z, halfMinusQySquared - Q.x * Q.x)),
                                   .pitch = FusionRadiansToDegrees(FusionAsin(2.0f * (Q.w * Q.y - Q.z * Q.x))),
                                   .yaw = FusionRadiansToDegrees(atan2f(Q.w * Q.z + Q.x * Q.y, halfMinusQySquared - Q.z * Q.z)),
                               }};
    return euler;
#undef Q
}

#endif

//------------------------------------------------------------------------------
// End of file
