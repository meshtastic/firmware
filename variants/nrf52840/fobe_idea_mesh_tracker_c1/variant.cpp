#include "variant.h"
#include "nrf.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[] = {
    // P0
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

    // P1
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47};

void initVariant()
{
    // LED
    pinMode(PIN_BUILTIN_LED, OUTPUT);
    digitalWrite(PIN_BUILTIN_LED, HIGH);

    // Button
    pinMode(PIN_BUTTON1, INPUT);

    // Battery Sense
    pinMode(BATTERY_PIN, INPUT);

    // Charging Detection
    pinMode(EXT_CHRG_DETECT, INPUT);

    // Peripheral Power
    pinMode(PIN_PERI_EN, OUTPUT);
    digitalWrite(PIN_PERI_EN, HIGH);

    // Motion
    pinMode(PIN_MOT_PWR, OUTPUT);
    digitalWrite(PIN_MOT_PWR, HIGH);
    pinMode(PIN_MOT_INT, INPUT);
}
