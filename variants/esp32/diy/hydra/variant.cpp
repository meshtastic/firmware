#include "variant.h"
#include "Arduino.h"

#ifdef USE_EBYTE_E22P // Ebyte E22P-868M30S and E22P-915M30S modules suport - RF Module Enable pin is always HIGH.
void initVariant()
{
    pinMode(E22P_ME, OUTPUT);
    digitalWrite(E22P_ME, HIGH);
}
#endif