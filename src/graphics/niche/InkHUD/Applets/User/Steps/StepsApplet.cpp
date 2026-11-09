#include "configuration.h"

#if defined(MESHTASTIC_INCLUDE_INKHUD) && !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && !MESHTASTIC_EXCLUDE_ACCELEROMETER

#include "StepsApplet.h"
#include "StepsIcon.h"
#include "UptimeClock.h"
#include "graphics/niche/Utils/FlashData.h"
#include "mesh/NodeDB.h"
#include "motion/AccelerometerThread.h"
#include <algorithm>
#include <cstdio>
#include <iterator>

using namespace NicheGraphics;
using namespace NicheGraphics::InkHUD;

namespace
{
constexpr uint32_t STRIDE_CM = 75;
constexpr uint32_t REFRESH_MS = 5000;
constexpr int16_t SHOE_ICON_SIZE = 32;
constexpr char GOAL_FILE[] = "steps-goal";

bool usesImperialUnits()
{
    return config.display.units == meshtastic_Config_DisplayConfig_DisplayUnits_IMPERIAL;
}

std::string formatCount(uint32_t count)
{
    auto text = std::to_string(count);
    for (size_t position = text.size(); position > 3;) {
        position -= 3;
        text.insert(position, 1, ',');
    }
    return text;
}

std::string formatDistance(uint32_t steps, bool imperial)
{
    const uint64_t centimeters = uint64_t(steps) * STRIDE_CM;
    const uint32_t centimetersPerLargeUnit = imperial ? 160934 : 100000;
    char text[32];
    if (centimeters < centimetersPerLargeUnit / 2) {
        const auto distance = imperial ? centimeters * 100 / 3048 : centimeters / 100;
        snprintf(text, sizeof(text), "~%lu %s", static_cast<unsigned long>(distance), imperial ? "ft" : "m");
    } else {
        const auto hundredths = centimeters * 100 / centimetersPerLargeUnit;
        snprintf(text, sizeof(text), "~%lu.%02lu %s", static_cast<unsigned long>(hundredths / 100),
                 static_cast<unsigned long>(hundredths % 100), imperial ? "mi" : "km");
    }
    return text;
}
} // namespace

StepsApplet::StepsApplet(AccelerometerThread &motion, MotionSensor &sensor)
    : OSThread("InkHUD Steps"), motion(motion), tracker(sensor.stepTracker())
{
    uint32_t storedGoal = 0;
    if (FlashData<uint32_t>::load(&storedGoal, GOAL_FILE) &&
        std::find(std::begin(GOALS), std::end(GOALS), storedGoal) != std::end(GOALS))
        goal = storedGoal;
    disable();
}

void StepsApplet::setGoal(uint32_t newGoal)
{
    if (newGoal == goal || std::find(std::begin(GOALS), std::end(GOALS), newGoal) == std::end(GOALS))
        return;
    goal = newGoal;
    FlashData<uint32_t>::save(&goal, GOAL_FILE);
    requestUpdate();
}

void StepsApplet::resetSession()
{
    motion.resetStepSession();
    requestUpdate();
}

void StepsApplet::onActivate()
{
    motion.setTrackingSteps(true);
}

void StepsApplet::onDeactivate()
{
    motion.setTrackingSteps(false);
}

void StepsApplet::onForeground()
{
    enabled = true;
    setIntervalFromNow(REFRESH_MS);
}

void StepsApplet::onBackground()
{
    disable();
}

int32_t StepsApplet::runOnce()
{
    const uint64_t minutes = tracker.elapsedMs(Time::getMillisMonotonic()) / 60000;
    if (isForeground() &&
        (tracker.steps() != renderedSteps || minutes != renderedMinutes || usesImperialUnits() != renderedImperial))
        requestUpdate();
    return REFRESH_MS;
}

void StepsApplet::onRender(bool)
{
    renderedSteps = tracker.steps();
    renderedMinutes = tracker.elapsedMs(Time::getMillisMonotonic()) / 60000;
    renderedImperial = usesImperialUnits();

    const int16_t margin = std::clamp(width() / 16, 5, 16);
    const int16_t right = width() - margin;
    const int16_t innerWidth = right - margin;
    if (innerWidth < 24)
        return;

    const int16_t line = fontSmall.lineHeight();
    const int16_t gap = std::clamp(height() / 18, 4, 14);
    const int16_t topMargin = std::clamp(height() / 16, 5, 16);
    int16_t y = std::min(margin, topMargin);
    setFont(fontSmall);
    const char *header = "STEPS (Resets on reboot)";
    const char *notice = "Resets on reboot";
    if (getTextWidth(header) > innerWidth)
        header = getTextWidth(notice) <= innerWidth ? notice : "STEPS";
    printAt(margin, y, header);
    y += line + gap;
    drawLine(margin, y, right - 1, y, BLACK);
    y += gap;

    const std::string count = formatCount(renderedSteps);
    setFont(fontLarge);
    if (getTextWidth(count) + SHOE_ICON_SIZE + gap > innerWidth || y + fontLarge.lineHeight() > height())
        setFont(fontMedium);
    if (getTextWidth(count) > innerWidth || y + fontMedium.lineHeight() > height())
        setFont(fontSmall);

    const int16_t countWidth = getTextWidth(count);
    const bool showIcon = countWidth + SHOE_ICON_SIZE + gap <= innerWidth && y + SHOE_ICON_SIZE <= height();
    const int16_t heroWidth = countWidth + (showIcon ? SHOE_ICON_SIZE + gap : 0);
    const int16_t heroHeight = std::max<int16_t>(getFont().lineHeight(), showIcon ? SHOE_ICON_SIZE + gap : 0);
    const int16_t left = (width() - heroWidth) / 2;
    if (showIcon)
        drawIconBitmapScaled(this, STEPS_ICON, left, y + (heroHeight - SHOE_ICON_SIZE) / 2, SHOE_ICON_SIZE, BLACK);
    printAt(left + (showIcon ? SHOE_ICON_SIZE + gap : 0), y + heroHeight / 2, count, LEFT, MIDDLE);
    y += heroHeight + gap;

    if (y + line + 10 > height())
        return;

    setFont(fontSmall);
    const auto goalLabel = "Goal " + formatCount(goal);
    const auto progressLabel = formatCount(uint64_t(renderedSteps) * 100 / goal) + "%";
    printAt(margin, y, goalLabel);
    if (getTextWidth(goalLabel) + getTextWidth(progressLabel) + gap <= innerWidth)
        printAt(right, y, progressLabel, RIGHT);
    y += line + 3;
    drawRoundRect(margin, y, innerWidth, 8, 3, BLACK);
    const int16_t progressWidth = (innerWidth - 4) * std::min(renderedSteps, goal) / goal;
    if (progressWidth > 0)
        fillRect(margin + 2, y + 2, progressWidth, 4, BLACK);
    y += 8 + gap;

    if (y + line > height())
        return;

    const auto distance = formatDistance(renderedSteps, renderedImperial);
    char duration[24];
    snprintf(duration, sizeof(duration), "%luh %02lum", static_cast<unsigned long>(renderedMinutes / 60),
             static_cast<unsigned long>(renderedMinutes % 60));

    const int16_t cardWidth = (innerWidth - gap) / 2;
    const int16_t cardHeight = line + fontMedium.lineHeight() + 12;
    if (cardWidth >= 76 && y + cardHeight <= height()) {
        drawRoundRect(margin, y, cardWidth, cardHeight, 5, BLACK);
        drawRoundRect(right - cardWidth, y, cardWidth, cardHeight, 5, BLACK);
        const int16_t firstCenter = margin + cardWidth / 2;
        const int16_t secondCenter = right - cardWidth / 2;
        printAt(firstCenter, y + 4, "DISTANCE", CENTER);
        printAt(secondCenter, y + 4, "SESSION", CENTER);
        setFont(fontMedium);
        if (getTextWidth(distance) > cardWidth - 8 || getTextWidth(duration) > cardWidth - 8)
            setFont(fontSmall);
        printAt(firstCenter, y + line + 7, distance, CENTER);
        printAt(secondCenter, y + line + 7, duration, CENTER);
    } else {
        printAt(margin, y, distance);
        if (getTextWidth(distance) + getTextWidth(duration) + gap <= innerWidth)
            printAt(right, y, duration, RIGHT);
    }
}

#endif
