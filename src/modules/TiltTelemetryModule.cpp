#include "TiltTelemetryModule.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && !MESHTASTIC_EXCLUDE_ACCELEROMETER && !defined(ARCH_STM32WL)

#include "../mesh/MeshService.h"
#include "../mesh/Router.h"
#include "../mesh/generated/meshtastic/portnums.pb.h"
#include "../motion/MotionSensor.h"
#include <math.h>

TiltTelemetryModule *tiltTelemetryModule = nullptr;

// Wire-format payload: 5 little-endian IEEE-754 floats, 20 bytes total
#pragma pack(push, 1)
struct TiltPayload {
    float roll;   // degrees from vertical (rotation about X-axis)
    float pitch;  // degrees from vertical (rotation about Y-axis)
    float x;      // raw accelerometer X in g
    float y;      // raw accelerometer Y in g
    float z;      // raw accelerometer Z in g
};
#pragma pack(pop)

static_assert(sizeof(TiltPayload) == 20, "TiltPayload must be 20 bytes");

TiltTelemetryModule::TiltTelemetryModule() : concurrency::OSThread("TiltTelemetry")
{
    setIntervalFromNow(60 * 1000); // wait 1 min for first accel sample to stabilise
}

int32_t TiltTelemetryModule::runOnce()
{
    float x, y, z;
    uint32_t ageMs;
    if (!MotionSensor::getLatestCompassAccelSample(x, y, z, ageMs) || ageMs > 30000) {
        // No valid sample yet — retry sooner
        return 30 * 1000;
    }

    // Tilt angles computed from gravity vector (static/slow-moving mount)
    float roll  = atan2f(y, sqrtf(x * x + z * z)) * (180.0f / M_PI);
    float pitch = atan2f(-x, sqrtf(y * y + z * z)) * (180.0f / M_PI);

    float dRoll  = fabsf(roll  - _lastRoll);
    float dPitch = fabsf(pitch - _lastPitch);
    bool moved     = (dRoll >= THRESHOLD_DEG || dPitch >= THRESHOLD_DEG);
    bool heartbeat = (millis() - _lastSentMs >= HEARTBEAT_MS);

    if (!_firstSend && !moved && !heartbeat)
        return POLL_MS;

    LOG_DEBUG("TiltTelemetry: roll=%.1f pitch=%.1f x=%.3f y=%.3f z=%.3f (moved=%d hb=%d)",
              roll, pitch, x, y, z, moved, heartbeat);

    meshtastic_MeshPacket *p = router->allocForSending();
    p->decoded.portnum = meshtastic_PortNum_PRIVATE_APP;
    p->want_ack = false;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;

    TiltPayload payload = {roll, pitch, x, y, z};
    static_assert(sizeof(payload) <= sizeof(p->decoded.payload.bytes), "TiltPayload too large");
    p->decoded.payload.size = sizeof(payload);
    memcpy(p->decoded.payload.bytes, &payload, sizeof(payload));

    service->sendToPhone(p);

    _lastRoll   = roll;
    _lastPitch  = pitch;
    _lastSentMs = millis();
    _firstSend  = false;

    return POLL_MS;
}

#endif
