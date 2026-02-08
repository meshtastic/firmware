#include "variant.h"
#include <PCA9557.h>

PCA9557 io(0x18, &Wire);

void earlyInitVariant()
{
    Wire.begin(48, 47);
    io.pinMode(PCA_PIN_EINK_EN, OUTPUT);
    io.pinMode(PCA_PIN_POWER_EN, OUTPUT);
    io.pinMode(PCA_LED_POWER, OUTPUT);
    io.pinMode(PCA_LED_USER, OUTPUT);
    io.pinMode(PCA_LED_ENABLE, OUTPUT);

    io.digitalWrite(PCA_PIN_POWER_EN, HIGH);
    io.digitalWrite(PCA_LED_USER, LOW);
    io.digitalWrite(PCA_LED_ENABLE, LOW);
}
