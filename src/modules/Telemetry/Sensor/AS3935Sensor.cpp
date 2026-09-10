#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<SparkFun_AS3935.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "AS3935Sensor.h"
#include "FSCommon.h"
#include "SPILock.h"
#include "SafeFile.h"
#include "TelemetrySensor.h"
#include "modules/Telemetry/EnvironmentTelemetry.h"
#include <SparkFun_AS3935.h>
#include <pb_decode.h>
#include <pb_encode.h>

namespace
{
// No attachInterrupt(): the interrupt latches until read, so polling can't miss it, and the
// I2C read itself isn't ISR-safe anyway. AS3935_IRQ is optional - see runOnce().
constexpr int32_t AS3935_CHECK_INTERVAL_MS = DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
constexpr uint8_t AS3935_DISTANCE_OUT_OF_RANGE = 0x3F;
} // namespace

// Fallback until an admin message sets one; 96pF is DFRobot's value for the SEN0290.
#ifndef AS3935_TUNING_CAP_PF
#define AS3935_TUNING_CAP_PF 96
#endif
static_assert(AS3935_TUNING_CAP_PF % 8 == 0 && AS3935_TUNING_CAP_PF <= 120,
              "AS3935_TUNING_CAP_PF must be a multiple of 8, at most 120 - tuneCap() silently ignores other values");

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

    // Oscillators are tuned to the antenna resonance; calibration affects strike detection thresholds.
    if (!lightning->calibrateOsc()) {
        LOG_WARN("%s: oscillator calibration failed", sensorName);
    }

    // Defaults match the library's own example, except outdoor mode. Disturbers are masked in
    // the chip - runOnce() polls every second, so an unmasked noisy site never goes quiet.
    lightning->setIndoorOutdoor(OUTDOOR);
    lightning->setNoiseLevel(2);
    lightning->watchdogThreshold(2);
    lightning->spikeRejection(2);
    lightning->maskDisturber(true);
    lightning->lightningThreshold(1);

    // Applied last: the RCO calibration above uses the antenna oscillator as its reference.
    if (!loadCalibrationData())
        as3935config.tuning_cap_pf = AS3935_TUNING_CAP_PF;
    if (!setTuningCap(as3935config.tuning_cap_pf)) {
        LOG_WARN("%s: bad stored cap %upF", sensorName, as3935config.tuning_cap_pf);
        setTuningCap(AS3935_TUNING_CAP_PF);
    }

#ifdef AS3935_IRQ
    pinMode(AS3935_IRQ, INPUT);
#endif
    // Drain anything already latched, so we don't report a strike that predates us.
    lightning->readInterruptReg();

    initI2CSensor();
    return status;
}

int32_t AS3935Sensor::runOnce()
{
#ifdef AS3935_IRQ
    // IRQ wired: only spend an I2C transaction once the pin says something is latched.
    if (digitalRead(AS3935_IRQ) == HIGH) {
        classifyPendingIrq();
    }
#else
    // I2C-only breakout: poll the register instead, it reads back 0 when nothing is pending.
    classifyPendingIrq();
#endif
    return AS3935_CHECK_INTERVAL_MS;
}

void AS3935Sensor::classifyPendingIrq()
{
    uint8_t interruptReason = lightning->readInterruptReg();
    switch (interruptReason) {
    case LIGHTNING: {
        strikes.add();
        uint8_t distance = lightning->distanceToStorm();
        if (distance != AS3935_DISTANCE_OUT_OF_RANGE) {
            lastDistanceKm = distance;
            LOG_INFO("%s: strike %dkm", sensorName, distance);
        } else {
            LOG_INFO("%s: strike, distance unknown", sensorName);
        }
        // No debounce here - EnvironmentTelemetryModule's airtime gate already paces every send.
        if (environmentTelemetryModule) {
            environmentTelemetryModule->requestImmediateSend();
        }
        break;
    }
    case NOISE_TO_HIGH:
        LOG_DEBUG("%s: noise floor high", sensorName);
        break;
    default:
        break;
    }
}

bool AS3935Sensor::setTuningCap(uint32_t pf)
{
    if (pf > 120 || pf % 8 != 0)
        return false;

    lightning->tuneCap(pf);
    as3935config.tuning_cap_pf = pf;
    // Readback, not pf: the only evidence the register write actually landed.
    LOG_INFO("%s: tuning cap %upF", sensorName, lightning->readTuneCap());
    return true;
}

AdminMessageHandleResult AS3935Sensor::handleAdminMessage(const meshtastic_MeshPacket &mp, meshtastic_AdminMessage *request,
                                                          meshtastic_AdminMessage *response)
{
    AdminMessageHandleResult result;
    result = AdminMessageHandleResult::NOT_HANDLED;

    switch (request->which_payload_variant) {
    case meshtastic_AdminMessage_sensor_config_tag:
        if (!request->sensor_config.has_as3935_config) {
            result = AdminMessageHandleResult::NOT_HANDLED;
            break;
        }

        if (request->sensor_config.as3935_config.has_set_tuning_cap_pf) {
            uint32_t pf = request->sensor_config.as3935_config.set_tuning_cap_pf;
            if (!setTuningCap(pf)) {
                LOG_ERROR("%s: bad cap %upF", sensorName, pf);
            } else if (!saveCalibrationData()) {
                LOG_WARN("%s: save failed", sensorName);
            }
        }

        result = AdminMessageHandleResult::HANDLED;
        break;

    default:
        result = AdminMessageHandleResult::NOT_HANDLED;
    }

    return result;
}

bool AS3935Sensor::saveCalibrationData()
{
    auto file = SafeFile(as3935ConfigFileName);
    bool okay = false;

    LOG_INFO("%s state write to %s", sensorName, as3935ConfigFileName);
    pb_ostream_t stream = {&writecb, static_cast<Print *>(&file), meshtastic_AS3935Config_size};

    if (!pb_encode(&stream, &meshtastic_AS3935Config_msg, &as3935config)) {
        LOG_ERROR("Can't encode protobuf %s", PB_GET_ERROR(&stream));
    } else {
        okay = true;
    }
    // Note: SafeFile::close() already acquires the lock and releases it internally
    okay &= file.close();

    return okay;
}

bool AS3935Sensor::loadCalibrationData()
{
    spiLock->lock();
    auto file = FSCom.open(as3935ConfigFileName, FILE_O_READ);
    bool okay = false;
    if (file) {
        LOG_INFO("%s state read from %s", sensorName, as3935ConfigFileName);
        pb_istream_t stream = {&readcb, &file, meshtastic_AS3935Config_size};
        if (!pb_decode(&stream, &meshtastic_AS3935Config_msg, &as3935config)) {
            LOG_ERROR("Can't decode protobuf %s", PB_GET_ERROR(&stream));
        } else {
            okay = true;
        }
        file.close();
    } else {
        LOG_INFO("No %s state found (File: %s)", sensorName, as3935ConfigFileName);
    }
    spiLock->unlock();
    return okay;
}

bool AS3935Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    uint32_t count = strikes.sum();
    measurement->variant.environment_metrics.has_lightning_strike_count_1h = true;
    measurement->variant.environment_metrics.lightning_strike_count_1h = count;
    // The distance belongs to the newest strike, so it expires when that strike leaves the window.
    if (count && lastDistanceKm >= 0) {
        measurement->variant.environment_metrics.has_lightning_distance_km = true;
        measurement->variant.environment_metrics.lightning_distance_km = lastDistanceKm;
    }
    return true;
}

#endif
