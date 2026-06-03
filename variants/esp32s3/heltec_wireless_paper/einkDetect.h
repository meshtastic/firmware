#pragma once

#include "configuration.h"

enum class EInkDetectionResult : uint8_t {
    LCMEN213EFC1 = 0, // V1.1
    E0213A367 = 1,    // V1.1.1, V1.2
};

EInkDetectionResult detectEInk()
{
    // Test 1: Logic of BUSY pin

    // Determines controller IC manufacturer
    // Fitipower: busy when LOW
    // Solomon Systech: busy when HIGH

    // Force display BUSY by holding reset pin active
    pinMode(PIN_EINK_RES, OUTPUT);
    digitalWrite(PIN_EINK_RES, LOW);

    delay(10);

    // Read whether pin is HIGH or LOW while busy
    pinMode(PIN_EINK_BUSY, INPUT);
    bool busyLogic = digitalRead(PIN_EINK_BUSY);

    // Test complete. Release pin
    pinMode(PIN_EINK_RES, INPUT);

    if (busyLogic == LOW)
        return EInkDetectionResult::LCMEN213EFC1;
    else // busy HIGH
        return EInkDetectionResult::E0213A367;
}