#if defined(Nodara)
#include "Nodara.h"

#include "McuDataInput.h"
#include <delay.h>

namespace nodara
{
namespace
{
McuDataInput mcuDataInput;
bool externalPowerOff = false;
} // namespace

bool isExternalPowerOff()
{
    return externalPowerOff;
}

void setExternalPowerOff(bool on)
{
    externalPowerOff = on;
}

void setup()
{
    mcuDataInput.setup();
    extern bool onPowerOn;
    while (!onPowerOn && millis() < 5500) {
        mcuDataInput.loop();
    }
}

void loop()
{
    mcuDataInput.loop();
}

} // namespace nodara
#endif
