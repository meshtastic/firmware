#include "GpioLogic.h"
#include "configuration.h"

/**
 * ledForceOn and ledForceOff both override the normal ledBlinker behavior (which is controlled by main)
 */
extern GpioVirtPin ledForceOn, ledBlink;