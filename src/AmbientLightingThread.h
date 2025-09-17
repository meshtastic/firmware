#include "Observer.h"
#include "configuration.h"

#ifdef HAS_NCP5623
#include <graphics/RAKled.h>
NCP5623 rgb;
#endif

#ifdef HAS_LP5562
#include <graphics/NomadStarLED.h>
LP5562 rgbw;
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
    explicit AmbientLightingThread(ScanI2C::DeviceType type) : OSThread("AmbientLighting")
    {
        notifyDeepSleepObserver.observe(&notifyDeepSleep); // Let us know when shutdown() is issued.

// Enables Ambient Lighting by default if conditions are meet.
#ifdef HAS_RGB_LED
#ifdef ENABLE_AMBIENTLIGHTING
        moduleConfig.ambient_lighting.led_state = true;
#endif
#endif
        // Uncomment to test module
        // moduleConfig.ambient_lighting.led_state = true;
        // moduleConfig.ambient_lighting.current = 10;
        // Default to a color based on our node number
        // moduleConfig.ambient_lighting.red = (myNodeInfo.my_node_num & 0xFF0000) >> 16;
        // moduleConfig.ambient_lighting.green = (myNodeInfo.my_node_num & 0x00FF00) >> 8;
        // moduleConfig.ambient_lighting.blue = myNodeInfo.my_node_num & 0x0000FF;

#if defined(HAS_NCP5623) || defined(HAS_LP5562)
        _type = type;
        if (_type == ScanI2C::DeviceType::NONE) {
            LOG_DEBUG("AmbientLighting Disable due to no RGB leds found on I2C bus");
            disable();
            return;
        }
#endif
#ifdef HAS_RGB_LED
        if (!moduleConfig.ambient_lighting.led_state) {
            LOG_DEBUG("AmbientLighting Disable due to moduleConfig.ambient_lighting.led_state OFF");
            disable();
            return;
        }
        LOG_DEBUG("AmbientLighting init");
#ifdef HAS_NCP5623
        if (_type == ScanI2C::NCP5623) {
            rgb.begin();
#endif
#ifdef HAS_LP5562
            if (_type == ScanI2C::LP5562) {
                rgbw.begin();
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
#if defined(HAS_NCP5623) || defined(HAS_LP5562)
            }
#endif
        }

      protected:
        int32_t runOnce() override
        {
#ifdef HAS_RGB_LED
#if defined(HAS_NCP5623) || defined(HAS_LP5562)
            if ((_type == ScanI2C::NCP5623 || _type == ScanI2C::LP5562) && moduleConfig.ambient_lighting.led_state) {
#endif
                setLighting();
                return 30000; // 30 seconds to reset from any animations that may have been running from Ext. Notification
#if defined(HAS_NCP5623) || defined(HAS_LP5562)
            }
#endif
#endif
            return disable();
        }

        // When shutdown() is issued, setLightingOff will be called.
        CallbackObserver<AmbientLightingThread, void *> notifyDeepSleepObserver =
            CallbackObserver<AmbientLightingThread, void *>(this, &AmbientLightingThread::setLightingOff);

      private:
        ScanI2C::DeviceType _type = ScanI2C::DeviceType::NONE;

        // Turn RGB lighting off, is used in junction to shutdown()
        int setLightingOff(void *unused)
        {
#ifdef HAS_NCP5623
            rgb.setCurrent(0);
            rgb.setRed(0);
            rgb.setGreen(0);
            rgb.setBlue(0);
            LOG_INFO("OFF: NCP5623 Ambient lighting");
#endif
#ifdef HAS_LP5562
            rgbw.setCurrent(0);
            rgbw.setRed(0);
            rgbw.setGreen(0);
            rgbw.setBlue(0);
            rgbw.setWhite(0);
            LOG_INFO("OFF: LP5562 Ambient lighting");
#endif
#ifdef HAS_NEOPIXEL
            pixels.clear();
            pixels.show();
            LOG_INFO("OFF: NeoPixel Ambient lighting");
#endif
#ifdef RGBLED_CA
            analogWrite(RGBLED_RED, 255 - 0);
            analogWrite(RGBLED_GREEN, 255 - 0);
            analogWrite(RGBLED_BLUE, 255 - 0);
            LOG_INFO("OFF: Ambient light RGB Common Anode");
#elif defined(RGBLED_RED)
        analogWrite(RGBLED_RED, 0);
        analogWrite(RGBLED_GREEN, 0);
        analogWrite(RGBLED_BLUE, 0);
        LOG_INFO("OFF: Ambient light RGB Common Cathode");
#endif
#ifdef UNPHONE
            unphone.rgb(0, 0, 0);
            LOG_INFO("OFF: unPhone Ambient lighting");
#endif
            return 0;
        }

        void setLighting()
        {
#ifdef HAS_NCP5623
            rgb.setCurrent(moduleConfig.ambient_lighting.current);
            rgb.setRed(moduleConfig.ambient_lighting.red);
            rgb.setGreen(moduleConfig.ambient_lighting.green);
            rgb.setBlue(moduleConfig.ambient_lighting.blue);
            LOG_DEBUG("Init NCP5623 Ambient light w/ current=%d, red=%d, green=%d, blue=%d",
                      moduleConfig.ambient_lighting.current, moduleConfig.ambient_lighting.red,
                      moduleConfig.ambient_lighting.green, moduleConfig.ambient_lighting.blue);
#endif
#ifdef HAS_LP5562
            rgbw.setCurrent(moduleConfig.ambient_lighting.current);
            rgbw.setRed(moduleConfig.ambient_lighting.red);
            rgbw.setGreen(moduleConfig.ambient_lighting.green);
            rgbw.setBlue(moduleConfig.ambient_lighting.blue);
            LOG_DEBUG("Init LP5562 Ambient light w/ current=%d, red=%d, green=%d, blue=%d", moduleConfig.ambient_lighting.current,
                      moduleConfig.ambient_lighting.red, moduleConfig.ambient_lighting.green, moduleConfig.ambient_lighting.blue);
#endif
#ifdef HAS_NEOPIXEL
            pixels.fill(pixels.Color(moduleConfig.ambient_lighting.red, moduleConfig.ambient_lighting.green,
                                     moduleConfig.ambient_lighting.blue),
                        0, NEOPIXEL_COUNT);

// RadioMaster Bandit has addressable LED at the two buttons
// this allow us to set different lighting for them in variant.h file.
#ifdef RADIOMASTER_900_BANDIT
#if defined(BUTTON1_COLOR) && defined(BUTTON1_COLOR_INDEX)
            pixels.fill(BUTTON1_COLOR, BUTTON1_COLOR_INDEX, 1);
#endif
#if defined(BUTTON2_COLOR) && defined(BUTTON2_COLOR_INDEX)
            pixels.fill(BUTTON2_COLOR, BUTTON2_COLOR_INDEX, 1);
#endif
#endif
            pixels.show();
            LOG_DEBUG("Init NeoPixel Ambient light w/ brightness(current)=%d, red=%d, green=%d, blue=%d",
                      moduleConfig.ambient_lighting.current, moduleConfig.ambient_lighting.red,
                      moduleConfig.ambient_lighting.green, moduleConfig.ambient_lighting.blue);
#endif
#ifdef RGBLED_CA
            analogWrite(RGBLED_RED, 255 - moduleConfig.ambient_lighting.red);
            analogWrite(RGBLED_GREEN, 255 - moduleConfig.ambient_lighting.green);
            analogWrite(RGBLED_BLUE, 255 - moduleConfig.ambient_lighting.blue);
            LOG_DEBUG("Init Ambient light RGB Common Anode w/ red=%d, green=%d, blue=%d", moduleConfig.ambient_lighting.red,
                      moduleConfig.ambient_lighting.green, moduleConfig.ambient_lighting.blue);
#elif defined(RGBLED_RED)
        analogWrite(RGBLED_RED, moduleConfig.ambient_lighting.red);
        analogWrite(RGBLED_GREEN, moduleConfig.ambient_lighting.green);
        analogWrite(RGBLED_BLUE, moduleConfig.ambient_lighting.blue);
        LOG_DEBUG("Init Ambient light RGB Common Cathode w/ red=%d, green=%d, blue=%d", moduleConfig.ambient_lighting.red,
                  moduleConfig.ambient_lighting.green, moduleConfig.ambient_lighting.blue);
#endif
#ifdef UNPHONE
            unphone.rgb(moduleConfig.ambient_lighting.red, moduleConfig.ambient_lighting.green,
                        moduleConfig.ambient_lighting.blue);
            LOG_DEBUG("Init unPhone Ambient light w/ red=%d, green=%d, blue=%d", moduleConfig.ambient_lighting.red,
                      moduleConfig.ambient_lighting.green, moduleConfig.ambient_lighting.blue);
#endif
        }
    };

} // namespace concurrency
