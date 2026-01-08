#include "configuration.h"
#if HAS_SCREEN
#include "ClockRenderer.h"
#include "gps/RTC.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/draw/UIRenderer.h"
#include "graphics/images.h"
#include "main.h"

#if !MESHTASTIC_EXCLUDE_BLUETOOTH
#include "nimble/NimbleBluetooth.h"
#endif

namespace graphics
{

namespace ClockRenderer
{

// Segment bitmaps for numerals 0-9 stored in flash to save RAM.
// Each row is a digit, each column is a segment state (1 = on, 0 = off).
// Segment layout reference:
//
//             ___1___
//           6 |     | 2
//             |_7___|
//           5 |     | 3
//             |___4_|
//
// Segment order: [1, 2, 3, 4, 5, 6, 7]
//
static const uint8_t PROGMEM digitSegments[10][7] = {
    {1, 1, 1, 1, 1, 1, 0}, // 0
    {0, 1, 1, 0, 0, 0, 0}, // 1
    {1, 1, 0, 1, 1, 0, 1}, // 2
    {1, 1, 1, 1, 0, 0, 1}, // 3
    {0, 1, 1, 0, 0, 1, 1}, // 4
    {1, 0, 1, 1, 0, 1, 1}, // 5
    {1, 0, 1, 1, 1, 1, 1}, // 6
    {1, 1, 1, 0, 0, 1, 0}, // 7
    {1, 1, 1, 1, 1, 1, 1}, // 8
    {1, 1, 1, 1, 0, 1, 1}  // 9
};

void drawSegmentedDisplayColon(OLEDDisplay *display, int x, int y, float scale)
{
    uint16_t segmentWidth = SEGMENT_WIDTH * scale;
    uint16_t segmentHeight = SEGMENT_HEIGHT * scale;

    uint16_t cellHeight = (segmentWidth * 2) + (segmentHeight * 3) + 8;

    uint16_t topAndBottomX = x + static_cast<uint16_t>(4 * scale);

    uint16_t quarterCellHeight = cellHeight / 4;

    uint16_t topY = y + quarterCellHeight;
    uint16_t bottomY = y + (quarterCellHeight * 3);

    display->fillRect(topAndBottomX, topY, segmentHeight, segmentHeight);
    display->fillRect(topAndBottomX, bottomY, segmentHeight, segmentHeight);
}

void drawSegmentedDisplayCharacter(OLEDDisplay *display, int x, int y, uint8_t number, float scale)
{
    // Read 7-segment pattern for the digit from flash
    uint8_t seg[7];
    for (uint8_t i = 0; i < 7; i++) {
        seg[i] = pgm_read_byte(&digitSegments[number][i]);
    }

    uint16_t segmentWidth = SEGMENT_WIDTH * scale;
    uint16_t segmentHeight = SEGMENT_HEIGHT * scale;

    // Precompute segment positions
    uint16_t segmentOneX = x + segmentHeight + 2;
    uint16_t segmentOneY = y;

    uint16_t segmentTwoX = segmentOneX + segmentWidth + 2;
    uint16_t segmentTwoY = segmentOneY + segmentHeight + 2;

    uint16_t segmentThreeX = segmentTwoX;
    uint16_t segmentThreeY = segmentTwoY + segmentWidth + 2 + segmentHeight + 2;

    uint16_t segmentFourX = segmentOneX;
    uint16_t segmentFourY = segmentThreeY + segmentWidth + 2;

    uint16_t segmentFiveX = x;
    uint16_t segmentFiveY = segmentThreeY;

    uint16_t segmentSixX = x;
    uint16_t segmentSixY = segmentTwoY;

    uint16_t segmentSevenX = segmentOneX;
    uint16_t segmentSevenY = segmentTwoY + segmentWidth + 2;

    // Draw only the active segments
    if (seg[0])
        drawHorizontalSegment(display, segmentOneX, segmentOneY, segmentWidth, segmentHeight);
    if (seg[1])
        drawVerticalSegment(display, segmentTwoX, segmentTwoY, segmentWidth, segmentHeight);
    if (seg[2])
        drawVerticalSegment(display, segmentThreeX, segmentThreeY, segmentWidth, segmentHeight);
    if (seg[3])
        drawHorizontalSegment(display, segmentFourX, segmentFourY, segmentWidth, segmentHeight);
    if (seg[4])
        drawVerticalSegment(display, segmentFiveX, segmentFiveY, segmentWidth, segmentHeight);
    if (seg[5])
        drawVerticalSegment(display, segmentSixX, segmentSixY, segmentWidth, segmentHeight);
    if (seg[6])
        drawHorizontalSegment(display, segmentSevenX, segmentSevenY, segmentWidth, segmentHeight);
}

void drawHorizontalSegment(OLEDDisplay *display, int x, int y, int width, int height)
{
    int halfHeight = height / 2;

    // draw central rectangle
    display->fillRect(x, y, width, height);

    // draw end triangles
    display->fillTriangle(x, y, x, y + height - 1, x - halfHeight, y + halfHeight);

    display->fillTriangle(x + width, y, x + width + halfHeight, y + halfHeight, x + width, y + height - 1);
}

void drawVerticalSegment(OLEDDisplay *display, int x, int y, int width, int height)
{
    int halfHeight = height / 2;

    // draw central rectangle
    display->fillRect(x, y, height, width);

    // draw end triangles
    display->fillTriangle(x + halfHeight, y - halfHeight, x + height - 1, y, x, y);

    display->fillTriangle(x, y + width, x + height - 1, y + width, x + halfHeight, y + width + halfHeight);
}

void drawDigitalClockFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    // === Set Title, Blank for Clock
    const char *titleStr = "";
    // === Header ===
    graphics::drawCommonHeader(display, x, y, titleStr, true, true);

    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice, true); // Display local timezone
    char timeString[16];
    int hour = 0;
    int minute = 0;
    int second = 0;

    if (rtc_sec > 0) {
        long hms = rtc_sec % SEC_PER_DAY;
        hms = (hms + SEC_PER_DAY) % SEC_PER_DAY;

        hour = hms / SEC_PER_HOUR;
        minute = (hms % SEC_PER_HOUR) / SEC_PER_MIN;
        second = (hms % SEC_PER_HOUR) % SEC_PER_MIN; // or hms % SEC_PER_MIN
    }

    bool isPM = hour >= 12;
    if (config.display.use_12h_clock) {
        hour %= 12;
        if (hour == 0) {
            hour = 12;
        }
        snprintf(timeString, sizeof(timeString), "%d:%02d", hour, minute);
    } else {
        snprintf(timeString, sizeof(timeString), "%02d:%02d", hour, minute);
    }

    // Format seconds string
    char secondString[8];
    snprintf(secondString, sizeof(secondString), "%02d", second);

    static bool scaleInitialized = false;
    static float scale = 0.75f;
    static float segmentWidth = SEGMENT_WIDTH * 0.75f;
    static float segmentHeight = SEGMENT_HEIGHT * 0.75f;

    if (!scaleInitialized) {
        float screenwidth_target_ratio = 0.80f; // Target 80% of display width (adjustable)
        float max_scale = 3.5f;                 // Safety limit to avoid runaway scaling
        float step = 0.05f;                     // Step increment per iteration

        float target_width = display->getWidth() * screenwidth_target_ratio;
        float target_height =
            display->getHeight() -
            ((currentResolution == ScreenResolution::High)
                 ? 46
                 : 33); // Be careful adjusting this number, we have to account for header and the text under the time

        float calculated_width_size = 0.0f;
        float calculated_height_size = 0.0f;

        while (true) {
            segmentWidth = SEGMENT_WIDTH * scale;
            segmentHeight = SEGMENT_HEIGHT * scale;

            calculated_width_size = segmentHeight + ((segmentWidth + (segmentHeight * 2) + 4) * 4);
            calculated_height_size = segmentHeight + ((segmentHeight + (segmentHeight * 2) + 4) * 2);

            if (calculated_width_size >= target_width || calculated_height_size >= target_height || scale >= max_scale) {
                break;
            }

            scale += step;
        }

        // If we overshot width, back off one step and recompute segment sizes
        if (calculated_width_size > target_width || calculated_height_size > target_height) {
            scale -= step;
            segmentWidth = SEGMENT_WIDTH * scale;
            segmentHeight = SEGMENT_HEIGHT * scale;
        }

        scaleInitialized = true;
    }

    // calculate hours:minutes string width
    size_t len = strlen(timeString);
    uint16_t timeStringWidth = len * 5;

    for (size_t i = 0; i < len; i++) {
        char character = timeString[i];

        if (character == ':') {
            timeStringWidth += segmentHeight;
        } else {
            timeStringWidth += segmentWidth + (segmentHeight * 2) + 4;
        }
    }

    uint16_t hourMinuteTextX = (display->getWidth() / 2) - (timeStringWidth / 2);
    uint16_t startingHourMinuteTextX = hourMinuteTextX;

    uint16_t hourMinuteTextY = (display->getHeight() / 2) - (((segmentWidth * 2) + (segmentHeight * 3) + 8) / 2) + 2;

    // iterate over characters in hours:minutes string and draw segmented characters
    for (size_t i = 0; i < len; i++) {
        char character = timeString[i];

        if (character == ':') {
            drawSegmentedDisplayColon(display, hourMinuteTextX, hourMinuteTextY, scale);

            hourMinuteTextX += segmentHeight + 6;
            if (scale >= 2.0f) {
                hourMinuteTextX += (uint16_t)(4.5f * scale);
            }
        } else {
            drawSegmentedDisplayCharacter(display, hourMinuteTextX, hourMinuteTextY, character - '0', scale);

            hourMinuteTextX += segmentWidth + (segmentHeight * 2) + 4;
        }

        hourMinuteTextX += 5;
    }

    // draw seconds string + AM/PM
    display->setFont(FONT_SMALL);
    int xOffset = -1;
    if (currentResolution == ScreenResolution::High) {
        xOffset = 0;
    }
    if (hour >= 10) {
        if (currentResolution == ScreenResolution::High) {
            xOffset += 32;
        } else {
            xOffset += 18;
        }
    }

    if (config.display.use_12h_clock) {
        display->drawString(startingHourMinuteTextX + xOffset, (display->getHeight() - hourMinuteTextY) - 1, isPM ? "pm" : "am");
    }

#ifndef USE_EINK
    xOffset = (currentResolution == ScreenResolution::High) ? 18 : 10;
    if (scale >= 2.0f) {
        xOffset -= (int)(4.5f * scale);
    }
    display->drawString(startingHourMinuteTextX + timeStringWidth - xOffset, (display->getHeight() - hourMinuteTextY) - 1,
                        secondString);
#endif

    graphics::drawCommonFooter(display, x, y);
}

// Draw an analog clock
void drawAnalogClockFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    // === Set Title, Blank for Clock
    const char *titleStr = "";
    // === Header ===
    graphics::drawCommonHeader(display, x, y, titleStr, true, true);

    // clock face center coordinates
    int16_t centerX = display->getWidth() / 2;
    int16_t centerY = display->getHeight() / 2;

    // clock face radius
    int16_t radius = (std::min(display->getWidth(), display->getHeight()) / 2) * 0.9;
#ifdef T_WATCH_S3
    radius = (display->getWidth() / 2) * 0.8;
#endif

    // noon (0 deg) coordinates (outermost circle)
    int16_t noonX = centerX;
    int16_t noonY = centerY - radius;

    // second hand radius and y coordinate (outermost circle)
    int16_t secondHandNoonY = noonY + 1;

    // tick mark outer y coordinate; (first nested circle)
    int16_t tickMarkOuterNoonY = secondHandNoonY;

    double secondsTickMarkInnerNoonY = noonY + ((currentResolution == ScreenResolution::High) ? 8 : 4);
    double hoursTickMarkInnerNoonY = noonY + ((currentResolution == ScreenResolution::High) ? 16 : 6);

    // minute hand y coordinate
    int16_t minuteHandNoonY = secondsTickMarkInnerNoonY + 4;

    // hour string y coordinate
    int16_t hourStringNoonY = minuteHandNoonY + 18;

    // hour hand radius and y coordinate
    int16_t hourHandRadius = radius * 0.35;
    if (currentResolution == ScreenResolution::High) {
        hourHandRadius = radius * 0.55;
    }
    int16_t hourHandNoonY = centerY - hourHandRadius;

    display->setColor(OLEDDISPLAY_COLOR::WHITE);
    display->drawCircle(centerX, centerY, radius);

    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice, true); // Display local timezone
    if (rtc_sec > 0) {
        int hour, minute, second;
        decomposeTime(rtc_sec, hour, minute, second);

        if (config.display.use_12h_clock) {
            bool isPM = hour >= 12;
            display->setFont(FONT_SMALL);
            int yOffset = (currentResolution == ScreenResolution::High) ? 1 : 0;
#ifdef USE_EINK
            yOffset += 3;
#endif
            display->drawString(centerX - (display->getStringWidth(isPM ? "pm" : "am") / 2), centerY + yOffset,
                                isPM ? "pm" : "am");
        }
        hour %= 12;
        if (hour == 0)
            hour = 12;

        int16_t degreesPerHour = 30;
        int16_t degreesPerMinuteOrSecond = 6;

        double hourBaseAngle = hour * degreesPerHour;
        double hourAngleOffset = ((double)minute / 60) * degreesPerHour;
        double hourAngle = radians(hourBaseAngle + hourAngleOffset);

        double minuteBaseAngle = minute * degreesPerMinuteOrSecond;
        double minuteAngleOffset = ((double)second / 60) * degreesPerMinuteOrSecond;
        double minuteAngle = radians(minuteBaseAngle + minuteAngleOffset);

        double secondAngle = radians(second * degreesPerMinuteOrSecond);

        double hourX = sin(-hourAngle) * (hourHandNoonY - centerY) + noonX;
        double hourY = cos(-hourAngle) * (hourHandNoonY - centerY) + centerY;

        double minuteX = sin(-minuteAngle) * (minuteHandNoonY - centerY) + noonX;
        double minuteY = cos(-minuteAngle) * (minuteHandNoonY - centerY) + centerY;

        double secondX = sin(-secondAngle) * (secondHandNoonY - centerY) + noonX;
        double secondY = cos(-secondAngle) * (secondHandNoonY - centerY) + centerY;

        display->setFont(FONT_MEDIUM);

        // draw minute and hour tick marks and hour numbers
        for (uint16_t angle = 0; angle < 360; angle += 6) {
            double angleInRadians = radians(angle);

            double sineAngleInRadians = sin(-angleInRadians);
            double cosineAngleInRadians = cos(-angleInRadians);

            double endX = sineAngleInRadians * (tickMarkOuterNoonY - centerY) + noonX;
            double endY = cosineAngleInRadians * (tickMarkOuterNoonY - centerY) + centerY;

            if (angle % degreesPerHour == 0) {
                double startX = sineAngleInRadians * (hoursTickMarkInnerNoonY - centerY) + noonX;
                double startY = cosineAngleInRadians * (hoursTickMarkInnerNoonY - centerY) + centerY;

                // draw hour tick mark
                display->drawLine(startX, startY, endX, endY);

                static char buffer[2];

                uint8_t hourInt = (angle / 30);

                if (hourInt == 0) {
                    hourInt = 12;
                }

                // hour number x offset needs to be adjusted for some cases
                int8_t hourStringXOffset;
                int8_t hourStringYOffset = 13;

                switch (hourInt) {
                case 3:
                    hourStringXOffset = 5;
                    break;
                case 9:
                    hourStringXOffset = 7;
                    break;
                case 10:
                case 11:
                    hourStringXOffset = 8;
                    break;
                case 12:
                    hourStringXOffset = 13;
                    break;
                default:
                    hourStringXOffset = 6;
                    break;
                }

                double hourStringX = (sineAngleInRadians * (hourStringNoonY - centerY) + noonX) - hourStringXOffset;
                double hourStringY = (cosineAngleInRadians * (hourStringNoonY - centerY) + centerY) - hourStringYOffset;

#ifdef T_WATCH_S3
                // draw hour number
                display->drawStringf(hourStringX, hourStringY, buffer, "%d", hourInt);
#else
#ifdef USE_EINK
                if (currentResolution == ScreenResolution::High) {
                    // draw hour number
                    display->drawStringf(hourStringX, hourStringY, buffer, "%d", hourInt);
                }
#else
                if (currentResolution == ScreenResolution::High &&
                    (hourInt == 3 || hourInt == 6 || hourInt == 9 || hourInt == 12)) {
                    // draw hour number
                    display->drawStringf(hourStringX, hourStringY, buffer, "%d", hourInt);
                }
#endif
#endif
            }

            if (angle % degreesPerMinuteOrSecond == 0) {
                double startX = sineAngleInRadians * (secondsTickMarkInnerNoonY - centerY) + noonX;
                double startY = cosineAngleInRadians * (secondsTickMarkInnerNoonY - centerY) + centerY;

                if (currentResolution == ScreenResolution::High) {
                    // draw minute tick mark
                    display->drawLine(startX, startY, endX, endY);
                }
            }
        }

        // draw hour hand
        display->drawLine(centerX, centerY, hourX, hourY);

        // draw minute hand
        display->drawLine(centerX, centerY, minuteX, minuteY);
#ifndef USE_EINK
        // draw second hand
        display->drawLine(centerX, centerY, secondX, secondY);
#endif
    }
    graphics::drawCommonFooter(display, x, y);
}

} // namespace ClockRenderer

} // namespace graphics
#endif