#include "BatteryCalibrationModule.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/ScreenFonts.h"
#include "power.h"
#include <algorithm>


BatteryCalibrationModule *batteryCalibrationModule;

BatteryCalibrationModule::BatteryCalibrationModule()
    : SinglePortModule("battery-calibration", meshtastic_PortNum_PRIVATE_APP)
{
    batteryCalibrationModule = this;
}

#if HAS_SCREEN
void BatteryCalibrationModule::startCalibration()
{
    calibrationActive = true;
    calibrationOcvValid = false;
    if (batteryCalibrationSampler) {
        batteryCalibrationSampler->resetSamples();
    }
}

void BatteryCalibrationModule::stopCalibration()
{
    calibrationActive = false;
}
#else
void BatteryCalibrationModule::startCalibration() {}
void BatteryCalibrationModule::stopCalibration() {}
#endif

bool BatteryCalibrationModule::persistCalibrationOcv()
{
    if (!calibrationOcvValid) {
        LOG_INFO("Battery calibration OCV not valid; skipping persistence");
        return false;
    }
    LOG_INFO("Persisting battery calibration OCV array");
    config.power.OCV_count = NUM_OCV_POINTS;
    for (size_t i = 0; i < NUM_OCV_POINTS; ++i) {
        config.power.OCV[i] = calibrationOcv[i];
        LOG_INFO("OCV[%u]=%u", static_cast<unsigned>(i), static_cast<unsigned>(calibrationOcv[i]));
    }
    LOG_INFO("Battery calibration OCV array persisted to config");
    return true;
}

#if HAS_SCREEN
void BatteryCalibrationModule::handleSampleUpdate()
{
    if (!calibrationActive) {
        return;
    }
    calibrationOcvValid = computeOcvFromSamples(calibrationOcv, NUM_OCV_POINTS);
}
#else
void BatteryCalibrationModule::handleSampleUpdate() {}
#endif

ProcessMessage BatteryCalibrationModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    (void)mp;
    return ProcessMessage::CONTINUE;
}

#if HAS_SCREEN
bool BatteryCalibrationModule::computeOcvFromSamples(uint16_t *ocvOut, size_t ocvCount)
{
    const BatteryCalibrationSampler::BatterySample *samples = nullptr;
    uint16_t sampleCount = 0;
    uint16_t sampleStart = 0;
    if (!batteryCalibrationSampler) {
        return false;
    }
    batteryCalibrationSampler->getSamples(samples, sampleCount, sampleStart);
    if (!samples || sampleCount < 2 || ocvCount < 2) {
        return false;
    }

    auto sampleAt = [&](uint16_t logicalIndex) -> const BatteryCalibrationSampler::BatterySample & {
        const uint16_t sampleIndex =
            static_cast<uint16_t>((sampleStart + logicalIndex) % BatteryCalibrationSampler::kMaxSamples);
        return samples[sampleIndex];
    };

    const uint32_t firstTimestamp = sampleAt(0).timestampMs;
    const uint32_t lastTimestamp = sampleAt(static_cast<uint16_t>(sampleCount - 1)).timestampMs;
    const uint32_t totalMs = (lastTimestamp >= firstTimestamp) ? (lastTimestamp - firstTimestamp) : 0;
    const float totalPoints = static_cast<float>(ocvCount - 1);

    for (size_t i = 0; i < ocvCount; ++i) {
        const float fraction = totalPoints > 0.0f ? static_cast<float>(i) / totalPoints : 0.0f;
        if (totalMs == 0) {
            const float samplePos = fraction * static_cast<float>(sampleCount - 1);
            const uint16_t lowerIndex = static_cast<uint16_t>(samplePos);
            const uint16_t upperIndex = static_cast<uint16_t>(std::min<uint16_t>(lowerIndex + 1, sampleCount - 1));
            const float interp = samplePos - static_cast<float>(lowerIndex);
            const uint16_t lowerVoltage = sampleAt(lowerIndex).voltageMv;
            const uint16_t upperVoltage = sampleAt(upperIndex).voltageMv;
            ocvOut[i] = static_cast<uint16_t>(lowerVoltage + interp * (upperVoltage - lowerVoltage));
            continue;
        }

        const uint32_t targetTimestamp = firstTimestamp + static_cast<uint32_t>(fraction * totalMs);
        const BatteryCalibrationSampler::BatterySample *prevSample = &sampleAt(0);
        const BatteryCalibrationSampler::BatterySample *nextSample = nullptr;
        for (uint16_t j = 1; j < sampleCount; ++j) {
            const BatteryCalibrationSampler::BatterySample &candidate = sampleAt(j);
            if (candidate.timestampMs >= targetTimestamp) {
                nextSample = &candidate;
                break;
            }
            prevSample = &candidate;
        }
        if (!nextSample) {
            ocvOut[i] = sampleAt(static_cast<uint16_t>(sampleCount - 1)).voltageMv;
            continue;
        }

        if (nextSample->timestampMs == prevSample->timestampMs) {
            ocvOut[i] = nextSample->voltageMv;
            continue;
        }

        const float timeFraction =
            static_cast<float>(targetTimestamp - prevSample->timestampMs) /
            static_cast<float>(nextSample->timestampMs - prevSample->timestampMs);
        const float voltage =
            static_cast<float>(prevSample->voltageMv) +
            timeFraction * (static_cast<float>(nextSample->voltageMv) - static_cast<float>(prevSample->voltageMv));
        ocvOut[i] = static_cast<uint16_t>(voltage);
    }
    return true;
}
#else
bool BatteryCalibrationModule::computeOcvFromSamples(uint16_t *, size_t)
{
    return false;
}
#endif

#if HAS_SCREEN
void BatteryCalibrationModule::computeGraphBounds(OLEDDisplay *display, int16_t x, int16_t y, int16_t &graphX, int16_t &graphY,
                                                  int16_t &graphW, int16_t &graphH)
{
    (void)y;
    const int *textPositions = graphics::getTextPositions(display);
    const int16_t lineY = textPositions[1];
    graphX = x;
    graphY = static_cast<int16_t>(lineY + FONT_HEIGHT_SMALL + 2);
    graphW = SCREEN_WIDTH;
    graphH = static_cast<int16_t>(SCREEN_HEIGHT - graphY);
    if (graphH < 0) {
        graphH = 0;
    }
}

void BatteryCalibrationModule::drawBatteryGraph(OLEDDisplay *display, int16_t graphX, int16_t graphY, int16_t graphW, int16_t graphH,
                                                const BatteryCalibrationSampler::BatterySample *samples, uint16_t sampleCount,
                                                uint16_t sampleStart, uint32_t minMv, uint32_t maxMv)
                                                {
    if (!samples || sampleCount < 2 || graphW <= 1 || graphH <= 1 || maxMv <= minMv) {
        return;
    }

    const uint32_t rangeMv = maxMv - minMv;
    const int32_t xSpan = graphW - 1;
    const int32_t ySpan = graphH - 1;
    const uint16_t maxIndex = static_cast<uint16_t>(sampleCount - 1);

    auto clampY = [&](int16_t yValue) -> int16_t {
        if (yValue < graphY) {
            return graphY;
        }
        const int16_t maxY = static_cast<int16_t>(graphY + ySpan);
        if (yValue > maxY) {
            return maxY;
        }
        return yValue;
    };

    auto voltageToY = [&](uint16_t voltageMv) -> int16_t {
        const uint32_t denom = (rangeMv == 0) ? 1 : rangeMv;
        const int32_t scaled = static_cast<int32_t>(static_cast<int32_t>(voltageMv) - static_cast<int32_t>(minMv)) * ySpan / denom;
        const int16_t yValue = static_cast<int16_t>(graphY + ySpan - scaled);
        return clampY(yValue);
    };

    const uint16_t prevIndex = sampleStart;
    int16_t prevX = graphX;
    int16_t prevY = voltageToY(samples[prevIndex].voltageMv);

    for (uint16_t i = 1; i < sampleCount; ++i) {
        const uint16_t sampleIndex = static_cast<uint16_t>((sampleStart + i) % BatteryCalibrationSampler::kMaxSamples);
        const int16_t currX = static_cast<int16_t>(graphX + (static_cast<int32_t>(i) * xSpan) / maxIndex);
        const int16_t currY = voltageToY(samples[sampleIndex].voltageMv);
        display->drawLine(prevX, prevY, currX, currY);
        prevX = currX;
        prevY = currY;
    }
}

void BatteryCalibrationModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    (void)state;
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);

    const char *titleStr = "Battery Calibration";

    graphics::drawCommonHeader(display, x, y, titleStr);

    char voltageStr[12] = {0};
    char percentStr[8] = {0};
    char durationStr[32] = {0};
    const bool hasBattery = powerStatus && powerStatus->getHasBattery();
    const bool calibrating = calibrationActive;
    if (hasBattery) {
        const int batV = powerStatus->getBatteryVoltageMv() / 1000;
        const int batCv = (powerStatus->getBatteryVoltageMv() % 1000) / 10;
        const int batMv = powerStatus->getBatteryVoltageMv(); //just for debug use mV
        //snprintf(voltageStr, sizeof(voltageStr), "%01d.%02dV", batV, batCv);
        snprintf(voltageStr, sizeof(voltageStr), "%04dmV", batMv); //just for debug use mV
        snprintf(percentStr, sizeof(percentStr), "%3d%%", powerStatus->getBatteryChargePercent());
    } else {
        snprintf(voltageStr, sizeof(voltageStr), "USB");
        snprintf(percentStr, sizeof(percentStr), "USB");
    }

    const int lineY = graphics::getTextPositions(display)[1];
    display->drawString(x, lineY, voltageStr);
    const int16_t percentX = static_cast<int16_t>(x + SCREEN_WIDTH - display->getStringWidth(percentStr));
    display->drawString(percentX, lineY, percentStr);

    uint32_t displayWindowMs = 0;
    const BatteryCalibrationSampler::BatterySample *samples = nullptr;
    uint16_t sampleCount = 0;
    uint16_t sampleStart = 0;
    if (batteryCalibrationSampler) {
        batteryCalibrationSampler->getSamples(samples, sampleCount, sampleStart);
    }
    if (samples && sampleCount >= 2) {
        const uint16_t firstIndex = sampleStart;
        const uint16_t lastIndex =
            static_cast<uint16_t>((sampleStart + sampleCount - 1) % BatteryCalibrationSampler::kMaxSamples);
        const uint32_t firstTimestamp = samples[firstIndex].timestampMs;
        const uint32_t lastTimestamp = samples[lastIndex].timestampMs;
        displayWindowMs = (lastTimestamp >= firstTimestamp) ? (lastTimestamp - firstTimestamp) : 0;
    }
    const uint32_t hourMs = 60 * 60 * 1000U;
 if (calibrating) {
        snprintf(durationStr, sizeof(durationStr), "Calibrating...");
    } else if (displayWindowMs >= hourMs && displayWindowMs % hourMs == 0) {
        snprintf(durationStr, sizeof(durationStr), "%luh", static_cast<unsigned long>(displayWindowMs / hourMs));
    } else {
        snprintf(durationStr, sizeof(durationStr), "%lum", static_cast<unsigned long>(displayWindowMs / 60000U));
    }
    const int16_t leftWidth = display->getStringWidth(voltageStr);
    const int16_t rightWidth = display->getStringWidth(percentStr);
    const int16_t durationWidth = display->getStringWidth(durationStr);
    const int16_t midStart = static_cast<int16_t>(x + leftWidth);
    const int16_t midWidth = static_cast<int16_t>(SCREEN_WIDTH - leftWidth - rightWidth);
    int16_t durationX = static_cast<int16_t>(midStart + (midWidth - durationWidth) / 2);
    if (durationX < midStart) {
        durationX = midStart;
    }
    if (durationX + durationWidth > percentX) {
        durationX = static_cast<int16_t>(percentX - durationWidth);
    }
    if (durationX >= x && durationX + durationWidth <= x + SCREEN_WIDTH) {
        display->drawString(durationX, lineY, durationStr);
    }

    const int ocvLineY = graphics::getTextPositions(display)[2];
    char ocvStr[96] = {0};
    if (power) {
        const uint16_t *ocvValues = power->getOcvArray();
        if (calibrationActive && calibrationOcvValid) {
            ocvValues = calibrationOcv;
        }
        int offset = snprintf(ocvStr, sizeof(ocvStr), "OCV:");
        for (size_t i = 0; i < NUM_OCV_POINTS && offset > 0 && static_cast<size_t>(offset) < sizeof(ocvStr); ++i) {
            const int written =
                snprintf(ocvStr + offset, sizeof(ocvStr) - static_cast<size_t>(offset), "%s%u",
                         i == 0 ? "" : ",", ocvValues[i]);
            if (written <= 0) {
                break;
            }
            offset += written;
        }
    } else {
        snprintf(ocvStr, sizeof(ocvStr), "OCV:N/A");
    }
    display->drawString(x, ocvLineY, ocvStr);

    int16_t graphX = 0;
    int16_t graphY = 0;
    int16_t graphW = 0;
    int16_t graphH = 0;
    computeGraphBounds(display, x, y, graphX, graphY, graphW, graphH);

    if (!hasBattery) {
        if (graphH > 0) {
            const char *placeholder = "No battery";
            const int16_t textX = static_cast<int16_t>(graphX + (graphW - display->getStringWidth(placeholder)) / 2);
            const int16_t textY = static_cast<int16_t>(graphY + (graphH - FONT_HEIGHT_SMALL) / 2);
            display->drawString(textX, textY, placeholder);
        }
        return;
    }

    uint32_t minMv = 0;
    uint32_t maxMv = 0;
    const uint16_t *ocvValues = power ? power->getOcvArray() : nullptr;
    if (ocvValues) {
        minMv = ocvValues[0];
        maxMv = ocvValues[0];
        for (size_t i = 1; i < NUM_OCV_POINTS; ++i) {
            minMv = std::min<uint32_t>(minMv, ocvValues[i]);
            maxMv = std::max<uint32_t>(maxMv, ocvValues[i]);
        }
        constexpr uint32_t marginMv = 200;
        minMv = (minMv > marginMv) ? (minMv - marginMv) : 0;
        maxMv += marginMv;
    } else {
        minMv = samples[sampleStart].voltageMv;
        maxMv = samples[sampleStart].voltageMv;
        for (uint16_t i = 1; i < sampleCount; ++i) {
            const uint16_t sampleIndex = static_cast<uint16_t>((sampleStart + i) % BatteryCalibrationSampler::kMaxSamples);
            const uint16_t voltageMv = samples[sampleIndex].voltageMv;
            minMv = std::min<uint32_t>(minMv, voltageMv);
            maxMv = std::max<uint32_t>(maxMv, voltageMv);
        }
    }

    drawBatteryGraph(display, graphX, graphY, graphW, graphH, samples, sampleCount, sampleStart, minMv, maxMv);
}
#endif