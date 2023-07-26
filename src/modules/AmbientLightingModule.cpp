#include "AmbientLightingModule.h"

void handleAmbientLightingConfig(const AmbientLightingConfig &config)
{
#ifdef RAK4630
    if (config.has_led_state()) {
        if (config.led_state()) {
            rgb.begin();
        } else {
            rgb.end(); // or any other method to turn off the LED
        }
    }

    if (config.has_current()) {
        rgb.setCurrent(config.current() * 31 / 100); // Scale the percentage to 1-31 range
    }

    if (config.has_red() || config.has_green() || config.has_blue()) {
        uint8_t red = config.has_red() ? config.red() * 255 / 100 : 0; // Scale the percentage to 0-255 range
        uint8_t green = config.has_green() ? config.green() * 255 / 100 : 0;
        uint8_t blue = config.has_blue() ? config.blue() * 255 / 100 : 0;
        rgb.setColor(red, green, blue);
    }
#endif
}