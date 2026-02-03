#include "variant.h"
#include <PCA9557.h>

PCA9557 io(0x18, &Wire1);

void earlyInitVariant()
{
    Wire1.begin(48, 47);
    io.pinMode(PCA_PIN_EINK_EN, OUTPUT);
    io.pinMode(PCA_PIN_POWER_EN, OUTPUT);
    io.digitalWrite(PCA_PIN_POWER_EN, HIGH);
}

void variant_shutdown()
{
    io.digitalWrite(PCA_PIN_POWER_EN, LOW);
}
