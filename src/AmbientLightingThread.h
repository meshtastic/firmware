#include "configuration.h"

#ifdef HAS_NCP5623
#include <graphics/RAKled.h>
NCP5623 rgb;
#endif

namespace concurrency
{
class AmbientLightingThread : public concurrency::OSThread
{
  public:
    explicit AmbientLightingThread(ScanI2C::DeviceType type) : OSThread("AmbientLightingThread")
    {
        // Uncomment to test module
        // moduleConfig.ambient_lighting.led_state = true;
        // moduleConfig.ambient_lighting.current = 10;
        // // Default to a color based on our node number
        // moduleConfig.ambient_lighting.red = (myNodeInfo.my_node_num & 0xFF0000) >> 16;
        // moduleConfig.ambient_lighting.green = (myNodeInfo.my_node_num & 0x00FF00) >> 8;
        // moduleConfig.ambient_lighting.blue = myNodeInfo.my_node_num & 0x0000FF;

#ifdef HAS_NCP5623
        _type = type;
        if (_type == ScanI2C::DeviceType::NONE) {
            LOG_DEBUG("AmbientLightingThread disabling due to no RGB leds found on I2C bus\n");
            disable();
            return;
        }
        if (!moduleConfig.ambient_lighting.led_state) {
            LOG_DEBUG("AmbientLightingThread disabling due to moduleConfig.ambient_lighting.led_state OFF\n");
            disable();
            return;
        }
        LOG_DEBUG("AmbientLightingThread initializing\n");
        if (_type == ScanI2C::NCP5623) {
            rgb.begin();
            setLighting();
        }
#endif
    }

  protected:
    int32_t runOnce() override
    {
#ifdef HAS_NCP5623
        if (_type == ScanI2C::NCP5623 && moduleConfig.ambient_lighting.led_state) {
            setLighting();
            return 30000; // 30 seconds to reset from any animations that may have been running from Ext. Notification
        } else {
            return disable();
        }
#else
        return disable();
#endif
    }

  private:
    ScanI2C::DeviceType _type = ScanI2C::DeviceType::NONE;

    void setLighting()
    {
#ifdef HAS_NCP5623
        rgb.setCurrent(moduleConfig.ambient_lighting.current);
        rgb.setRed(moduleConfig.ambient_lighting.red);
        rgb.setGreen(moduleConfig.ambient_lighting.green);
        rgb.setBlue(moduleConfig.ambient_lighting.blue);
        LOG_DEBUG("Initializing Ambient lighting w/ current=%d, red=%d, green=%d, blue=%d\n",
                  moduleConfig.ambient_lighting.current, moduleConfig.ambient_lighting.red, moduleConfig.ambient_lighting.green,
                  moduleConfig.ambient_lighting.blue);
#endif
    }
};

} // namespace concurrency