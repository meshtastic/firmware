#include "configuration.h"

#ifdef HAS_NCP5623
#include <graphics/RAKled.h>
NCP5623 rgb;
#endif

#ifdef HAS_NEOPIXEL
#include <graphics/NeoPixel.h>
Adafruit_NeoPixel pixels(NEOPIXEL_COUNT, NEOPIXEL_DATA, NEOPIXEL_TYPE);
#endif

#ifdef UNPHONE
#include "unPhone.h"
extern unPhone unphone;
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
#endif
#if defined(HAS_NCP5623) || defined(RGBLED_RED) || defined(HAS_NEOPIXEL) || defined(UNPHONE)
        if (!moduleConfig.ambient_lighting.led_state) {
            LOG_DEBUG("AmbientLightingThread disabling due to moduleConfig.ambient_lighting.led_state OFF\n");
            disable();
            return;
        }
        LOG_DEBUG("AmbientLightingThread initializing\n");
#ifdef HAS_NCP5623
        if (_type == ScanI2C::NCP5623) {
            rgb.begin();
#endif
#ifdef RGBLED_RED
            pinMode(RGBLED_RED, OUTPUT);
            pinMode(RGBLED_GREEN, OUTPUT);
            pinMode(RGBLED_BLUE, OUTPUT);
#endif
#ifdef HAS_NEOPIXEL
            pixels.begin(); // Initialise the pixel(s)
            pixels.clear(); // Set all pixel colors to 'off'
            pixels.setBrightness(moduleConfig.ambient_lighting.current);
#endif
            setLighting();
#endif
#ifdef HAS_NCP5623
        }
#endif
    }

  protected:
    int32_t runOnce() override
    {
#if defined(HAS_NCP5623) || defined(RGBLED_RED) || defined(HAS_NEOPIXEL) || defined(UNPHONE)
#ifdef HAS_NCP5623
        if (_type == ScanI2C::NCP5623 && moduleConfig.ambient_lighting.led_state) {
#endif
            setLighting();
            return 30000; // 30 seconds to reset from any animations that may have been running from Ext. Notification
#ifdef HAS_NCP5623
        }
#endif
#endif
        return disable();
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
        LOG_DEBUG("Initializing NCP5623 Ambient lighting w/ current=%d, red=%d, green=%d, blue=%d\n",
                  moduleConfig.ambient_lighting.current, moduleConfig.ambient_lighting.red, moduleConfig.ambient_lighting.green,
                  moduleConfig.ambient_lighting.blue);
#endif
#ifdef HAS_NEOPIXEL
        pixels.fill(pixels.Color(moduleConfig.ambient_lighting.red, moduleConfig.ambient_lighting.green,
                                 moduleConfig.ambient_lighting.blue),
                    0, NEOPIXEL_COUNT);
        pixels.show();
        LOG_DEBUG("Initializing NeoPixel Ambient lighting w/ brightness(current)=%d, red=%d, green=%d, blue=%d\n",
                  moduleConfig.ambient_lighting.current, moduleConfig.ambient_lighting.red, moduleConfig.ambient_lighting.green,
                  moduleConfig.ambient_lighting.blue);
#endif
#ifdef RGBLED_CA
        analogWrite(RGBLED_RED, 255 - moduleConfig.ambient_lighting.red);
        analogWrite(RGBLED_GREEN, 255 - moduleConfig.ambient_lighting.green);
        analogWrite(RGBLED_BLUE, 255 - moduleConfig.ambient_lighting.blue);
        LOG_DEBUG("Initializing Ambient lighting RGB Common Anode w/ red=%d, green=%d, blue=%d\n",
                  moduleConfig.ambient_lighting.red, moduleConfig.ambient_lighting.green, moduleConfig.ambient_lighting.blue);
#elif defined(RGBLED_RED)
        analogWrite(RGBLED_RED, moduleConfig.ambient_lighting.red);
        analogWrite(RGBLED_GREEN, moduleConfig.ambient_lighting.green);
        analogWrite(RGBLED_BLUE, moduleConfig.ambient_lighting.blue);
        LOG_DEBUG("Initializing Ambient lighting RGB Common Cathode w/ red=%d, green=%d, blue=%d\n",
                  moduleConfig.ambient_lighting.red, moduleConfig.ambient_lighting.green, moduleConfig.ambient_lighting.blue);
#endif
#ifdef UNPHONE
        unphone.rgb(moduleConfig.ambient_lighting.red, moduleConfig.ambient_lighting.green, moduleConfig.ambient_lighting.blue);
        LOG_DEBUG("Initializing unPhone Ambient lighting w/ red=%d, green=%d, blue=%d\n", moduleConfig.ambient_lighting.red,
                  moduleConfig.ambient_lighting.green, moduleConfig.ambient_lighting.blue);
#endif
    }
};

} // namespace concurrency