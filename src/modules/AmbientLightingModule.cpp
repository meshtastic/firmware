#include "AmbientLightingModule.h"
#include "mesh/generated/meshtastic/module_config.pb.h"

void handleModuleConfig(const meshtastic::ModuleConfig &config)
{
#ifdef HAS_NCP5623
    if (config.has_ambient_lighting()) {
        const auto &ambient_lighting_config = config.ambient_lighting();
        if (ambient_lighting_config.has_current()) {
            rgb.setCurrent(ambient_lighting_config.current() * 31 / 100); // Scale the percentage to 1-31 range
        }

        if (ambient_lighting_config.has_red() || ambient_lighting_config.has_green() || ambient_lighting_config.has_blue()) {
            uint8_t red = ambient_lighting_config.has_red() ? ambient_lighting_config.red() * 255 / 100
                                                            : 0; // Scale the percentage to 0-255 range
            uint8_t green = ambient_lighting_config.has_green() ? ambient_lighting_config.green() * 255 / 100 : 0;
            uint8_t blue = ambient_lighting_config.has_blue() ? ambient_lighting_config.blue() * 255 / 100 : 0;
            rgb.setColor(red, green, blue);
        }
    }
#endif
}