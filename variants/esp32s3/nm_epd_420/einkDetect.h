#pragma once

#include "DebugConfiguration.h"
#include "configuration.h"

enum class EInkDetectionResult : uint8_t {
    SSD1683 = 0,
    UC8179 = 1,
};

inline EInkDetectionResult detectEInk()
{
    pinMode(PIN_EINK_BUSY, INPUT_PULLUP);
    delay(2);

    pinMode(PIN_EINK_RES, OUTPUT);
    digitalWrite(PIN_EINK_RES, HIGH);
    delay(5);
    digitalWrite(PIN_EINK_RES, LOW);
    delay(10);
    digitalWrite(PIN_EINK_RES, HIGH);
    pinMode(PIN_EINK_BUSY, INPUT_PULLUP);

    uint8_t lowCount = 0;
    uint8_t highCount = 0;
    for (uint8_t sample = 0; sample < 80; ++sample) {
        if (digitalRead(PIN_EINK_BUSY) == LOW)
            ++lowCount;
        else
            ++highCount;
        delay(1);
    }

    EInkDetectionResult result = lowCount < 12 ? EInkDetectionResult::UC8179 : EInkDetectionResult::SSD1683;
    LOG_INFO("NM-EPD-420 classic EPD controller=%s BUSY low=%u high=%u",
             result == EInkDetectionResult::UC8179 ? "UC8179" : "SSD1683", lowCount, highCount);
    return result;
}
