#include "variant.h"
#include "configuration.h"
#include "nrf.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[] = {
    2, 3, 28, 29, 4, 5, 43, 44, 45, 46, 47,
    26, 6, 30, 14,
    40, 27, 7, 11,
    42, 32, 16,
    13, 17,
    21, 25, 20, 24, 22, 23,
    9, 10,
    31,
};

void initVariant()
{
    pinMode(VBAT_ENABLE, OUTPUT);
    digitalWrite(VBAT_ENABLE, LOW);

    pinMode(HICHG, OUTPUT);
    digitalWrite(HICHG, LOW);

    pinMode(PIN_LED1, OUTPUT);
    ledOff(PIN_LED1);
    pinMode(PIN_LED2, OUTPUT);
    ledOff(PIN_LED2);
    pinMode(PIN_LED3, OUTPUT);
    ledOff(PIN_LED3);
}
