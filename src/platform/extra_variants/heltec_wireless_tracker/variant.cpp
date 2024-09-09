#include "configuration.h"

#ifdef _VARIANT_HELTEC_WIRELESS_TRACKER

#include "GPS.h"
#include "GpioLogic.h"
#include "graphics/TFTDisplay.h"

// Heltec tracker specific init
void lateInitVariant()
{
    // LOG_DEBUG("Heltec tracker initVariant\n");
#ifdef VEXT_ENABLE
    GpioPin *hwEnable = new GpioHwPin(VEXT_ENABLE);

#ifdef MESHTASTIC_EXCLUDE_GPS
    GpioVirtPin *virtGpsEnable = new GpioVirtPin();
#else
    GpioVirtPin *virtGpsEnable = gps ? gps->enablePin : new GpioVirtPin();
#endif

    // On this board we are actually using the backlightEnable signal to already be controlling a physical enable to the
    // display controller.  But we'd _ALSO_ like to have that signal drive a virtual GPIO.  So nest it as needed.
    GpioVirtPin *virtScreenEnable = new GpioVirtPin();
    if (TFTDisplay::backlightEnable) {
        GpioPin *physScreenEnable = TFTDisplay::backlightEnable;
        GpioPin *splitter = new GpioSplitter(virtScreenEnable, physScreenEnable);
        TFTDisplay::backlightEnable = splitter;

        // Assume screen is initially powered
        splitter->set(true);
    }

    // If either the GPS or the screen is on, turn on the external power regulator
    new GpioBinaryTransformer(virtGpsEnable, virtScreenEnable, hwEnable, GpioBinaryTransformer::Or);
#endif
}

#endif