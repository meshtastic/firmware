#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<SparkFun_AS3935.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "AS3935Sensor.h"
#include "TelemetrySensor.h"
#include "modules/Telemetry/EnvironmentTelemetry.h"
#include <SparkFun_AS3935.h>
#include <Throttle.h>

namespace
{
// No attachInterrupt(): the IRQ line stays asserted until read, so polling can't miss it,
// and the I2C read itself isn't ISR-safe anyway.
constexpr int32_t AS3935_CHECK_INTERVAL_MS = DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
constexpr uint8_t AS3935_DISTANCE_OUT_OF_RANGE = 0x3F;
// Strikes accumulate over a rolling window, reset by elapsed time rather than on
// getMetrics() (which also fires when replying to a peer's telemetry request).
constexpr uint32_t AS3935_STRIKE_WINDOW_MS = 60UL * 60UL * 1000; // 1 hour
} // namespace

AS3935Sensor::AS3935Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_AS3935, "AS3935") {}

AS3935Sensor::~AS3935Sensor()
{
    if (lightning) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
        delete lightning;
#pragma GCC diagnostic pop
        lightning = nullptr;
    }
}

bool AS3935Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);

    lightning = new SparkFun_AS3935(dev->address.address);
    status = lightning->begin(*bus);
    if (!status) {
        initI2CSensor();
        return status;
    }

    // Defaults match the library's own example, except outdoor mode and unmasked
    // disturbers (kept visible in the log).
    lightning->setIndoorOutdoor(OUTDOOR);
    lightning->setNoiseLevel(2);
    lightning->watchdogThreshold(2);
    lightning->spikeRejection(2);
    lightning->maskDisturber(false);
    lightning->lightningThreshold(1);

#ifdef AS3935_IRQ
    pinMode(AS3935_IRQ, INPUT);
#endif

    windowStartMs = millis();
    initI2CSensor();
    return status;
}

int32_t AS3935Sensor::runOnce()
{
#ifdef AS3935_IRQ
    if (digitalRead(AS3935_IRQ) == HIGH) {
        classifyPendingIrq();
    }
#endif
    if (!Throttle::isWithinTimespanMs(windowStartMs, AS3935_STRIKE_WINDOW_MS)) {
        strikeCountWindow = 0;
        lastDistanceKm = -1;
        windowStartMs = millis();
    }
    return AS3935_CHECK_INTERVAL_MS;
}

void AS3935Sensor::classifyPendingIrq()
{
    uint8_t interruptReason = lightning->readInterruptReg();
    switch (interruptReason) {
    case LIGHTNING: {
        strikeCountWindow++;
        uint8_t distance = lightning->distanceToStorm();
        if (distance != AS3935_DISTANCE_OUT_OF_RANGE) {
            lastDistanceKm = distance;
            LOG_INFO("%s: lightning strike detected, distance=%dkm", sensorName, distance);
        } else {
            LOG_INFO("%s: lightning strike detected, distance unknown (out of range)", sensorName);
        }
        // No debounce here - EnvironmentTelemetryModule's airtime gate already paces every send.
        if (environmentTelemetryModule) {
            environmentTelemetryModule->requestImmediateSend();
        }
        break;
    }
    case DISTURBER_DETECT:
        LOG_DEBUG("%s: disturber detected (ignored)", sensorName);
        break;
    case NOISE_TO_HIGH:
        LOG_DEBUG("%s: noise floor too high", sensorName);
        break;
    default:
        break;
    }
}

bool AS3935Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_lightning_strike_count_1h = true;
    measurement->variant.environment_metrics.lightning_strike_count_1h = strikeCountWindow;
    if (lastDistanceKm >= 0) {
        measurement->variant.environment_metrics.has_lightning_distance_km = true;
        measurement->variant.environment_metrics.lightning_distance_km = lastDistanceKm;
    }
    return true;
}

#endif
