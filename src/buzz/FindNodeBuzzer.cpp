#include "FindNodeBuzzer.h"
#include "NodeDB.h"
#include "buzz.h"
#include "configuration.h"
#include "mesh/Throttle.h"
#include <Arduino.h>
#include <limits.h>

namespace
{
constexpr uint32_t FIND_NODE_REPEAT_INTERVAL_MS = 2000;
}

FindNodeBuzzer *findNodeBuzzer;

FindNodeBuzzer::FindNodeBuzzer() : concurrency::OSThread("FindNodeBuzzer", INT32_MAX) {}

FindNodeBuzzer::Result FindNodeBuzzer::start()
{
    if (!hasFindNodeBuzzer()) {
        return Result::NoBuzzer;
    }

    if (config.device.buzzer_mode == meshtastic_Config_DeviceConfig_BuzzerMode_DISABLED) {
        return Result::BuzzerDisabled;
    }

    startMsec = millis();
    durationMsec = DEFAULT_DURATION_SECONDS * 1000U;

    if (!playFindNodeBuzzer()) {
        stop();
        return Result::NoBuzzer;
    }

    setIntervalFromNow(FIND_NODE_REPEAT_INTERVAL_MS);
    return Result::Started;
}

FindNodeBuzzer::Result FindNodeBuzzer::stop()
{
    startMsec = 0;
    durationMsec = 0;
    setIntervalFromNow(INT32_MAX);
    return Result::Stopped;
}

bool FindNodeBuzzer::isActive() const
{
    return durationMsec != 0 && Throttle::isWithinTimespanMs(startMsec, durationMsec);
}

int32_t FindNodeBuzzer::runOnce()
{
    if (!isActive()) {
        stop();
        return INT32_MAX;
    }

    if (config.device.buzzer_mode == meshtastic_Config_DeviceConfig_BuzzerMode_DISABLED || !playFindNodeBuzzer()) {
        stop();
        return INT32_MAX;
    }

    if (!isActive()) {
        stop();
        return INT32_MAX;
    }

    return FIND_NODE_REPEAT_INTERVAL_MS;
}
