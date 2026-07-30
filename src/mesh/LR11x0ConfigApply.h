#pragma once

#include <stdint.h>

enum class LR11x0ApplyStep : uint8_t {
    STANDBY,
    SPREADING_FACTOR,
    BANDWIDTH,
    CODING_RATE,
    SYNC_WORD,
    PREAMBLE,
    FREQUENCY,
    OUTPUT_POWER,
    RX_GAIN,
    START_RECEIVE,
    COUNT,
};

struct LR11x0ConfigApplyParams {
    uint8_t spreadingFactor;
    float bandwidth;
    uint8_t codingRate;
    uint8_t syncWord;
    uint16_t preambleLength;
    float frequency;
    int8_t outputPower;
    bool boostedGain;
    bool wideBand;
    int8_t maxPower;
};

inline const char *lr11x0ApplyStepName(LR11x0ApplyStep step)
{
    static const char *const names[] = {"standby",  "spreading factor", "bandwidth",    "coding rate", "sync word",
                                        "preamble", "frequency",        "output power", "RX gain",     "start receive"};
    return step < LR11x0ApplyStep::COUNT ? names[static_cast<uint8_t>(step)] : "unknown";
}

template <typename Ops> class LR11x0ConfigApply
{
  public:
    static int run(Ops &ops, const LR11x0ConfigApplyParams &params, LR11x0ApplyStep *failedStep = nullptr)
    {
        int error = ops.standby();
        if (error != 0)
            return fail(failedStep, LR11x0ApplyStep::STANDBY, error);

        error = ops.setSpreadingFactor(params.spreadingFactor);
        if (error != 0)
            return fail(failedStep, LR11x0ApplyStep::SPREADING_FACTOR, error);

        error = ops.setBandwidth(params.bandwidth, params.wideBand);
        if (error != 0)
            return fail(failedStep, LR11x0ApplyStep::BANDWIDTH, error);

        error = ops.setCodingRate(params.codingRate, params.codingRate != 7);
        if (error != 0)
            return fail(failedStep, LR11x0ApplyStep::CODING_RATE, error);

        error = ops.setSyncWord(params.syncWord);
        if (error != 0)
            return fail(failedStep, LR11x0ApplyStep::SYNC_WORD, error);

        error = ops.setPreambleLength(params.preambleLength);
        if (error != 0)
            return fail(failedStep, LR11x0ApplyStep::PREAMBLE, error);

        error = ops.setFrequency(params.frequency);
        if (error != 0)
            return fail(failedStep, LR11x0ApplyStep::FREQUENCY, error);

        error = ops.setOutputPower(params.outputPower);
        if (error != 0)
            return fail(failedStep, LR11x0ApplyStep::OUTPUT_POWER, error);

        error = ops.setRxBoostedGainMode(params.boostedGain);
        if (error != 0)
            return fail(failedStep, LR11x0ApplyStep::RX_GAIN, error);

        error = ops.startReceive();
        if (error != 0)
            return fail(failedStep, LR11x0ApplyStep::START_RECEIVE, error);

        return error;
    }

  private:
    static int fail(LR11x0ApplyStep *failedStep, LR11x0ApplyStep step, int error)
    {
        if (failedStep != nullptr)
            *failedStep = step;
        return error;
    }
};
