#include "variant.h"
#include "Arduino.h"

void earlyInitVariant()
{
    pinMode(USER_LED, OUTPUT);
    digitalWrite(USER_LED, HIGH ^ LED_STATE_ON);
}