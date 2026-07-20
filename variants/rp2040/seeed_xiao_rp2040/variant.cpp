#include <Arduino.h>

// Turn off the green and blue LEDs, which are on by default.
void initVariant()
{
    pinMode(PIN_LED_G, OUTPUT);
    pinMode(PIN_LED_B, OUTPUT);
    digitalWrite(PIN_LED_G, HIGH);
    digitalWrite(PIN_LED_B, HIGH);
}