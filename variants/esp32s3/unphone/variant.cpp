// meshtastic/firmware/variants/unphone/variant.cpp

#include "unPhone.h"
unPhone unphone = unPhone("meshtastic_unphone");

void initVariant()
{
    unphone.begin(); // initialise hardware etc.
    unphone.store(unphone.buildTime);
    unphone.printWakeupReason(); // what woke us up? (stored, not printed :|)
    unphone.checkPowerSwitch();  // if power switch is off, shutdown
    unphone.backlight(false);    // setup backlight and make sure its off
    unphone.expanderPower(true); // enable power to expander / hat / sheild

    for (int i = 0; i < 3; i++) { // buzz a bit
        unphone.vibe(true);
        delay(150);
        unphone.vibe(false);
        delay(150);
    }
}