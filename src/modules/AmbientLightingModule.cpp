#include "AmbientLightingModule.h"

void AmbientLightingModule::handleConfig(const meshtastic::ModuleConfig &config)
{
#ifdef HAS_NCP5623
    if (config.has_ambient_lighting_config()) {
        const meshtastic::AmbientLightingConfig &ambientLightingConfig = config.ambient_lighting_config();

        // Replace with actual method to find the RGB device
        // Assuming ScanI2C and DeviceFound are accessible here
        DeviceFound rgb_found = ScanI2C.find(ScanI2C::DeviceType::NCP5623);

        if (rgb_found.type == ScanI2C::NCP5623) {
            rgb.begin(); // Start the RGB LED

            if (ambientLightingConfig.has_current()) {
                rgb.setCurrent(ambientLightingConfig.current());
            }

            if (ambientLightingConfig.has_red() || ambientLightingConfig.has_green() || ambientLightingConfig.has_blue()) {
                uint8_t red = ambientLightingConfig.has_red() ? ambientLightingConfig.red() : 0;
                uint8_t green = ambientLightingConfig.has_green() ? ambientLightingConfig.green() : 0;
                uint8_t blue = ambientLightingConfig.has_blue() ? ambientLightingConfig.blue() : 0;
                rgb.setColor(red, green, blue);
            }
        }
    }
#endif
}