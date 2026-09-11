#include "DeviceUiPolicy.h"

#include "graphics/ScreenFonts.h"

namespace
{
const DeviceUiMetrics defaultMetrics = {
    false,
    FONT_SMALL_LOCAL,
    FONT_SMALL_LOCAL,
    deviceUiDefaultFontSmall,
    deviceUiDefaultFontSmall,
    deviceUiDefaultFontMedium,
    deviceUiDefaultFontLarge,
};

class DefaultEInkUiPolicy final : public DeviceUiPolicy
{
  public:
    const DeviceUiMetrics &metrics() const override { return defaultMetrics; }
    bool supportsBrightness() const override
    {
#if defined(ST7789_CS) || defined(USE_OLED) || defined(USE_SSD1306) || defined(USE_SH1106) || defined(USE_SH1107)
        return true;
#else
        return false;
#endif
    }
};

const DefaultEInkUiPolicy defaultPolicy;
} // namespace

const DeviceUiPolicy &getDefaultDeviceUiPolicy()
{
    return defaultPolicy;
}

const DeviceUiMetrics &getDefaultDeviceUiMetrics()
{
    return defaultMetrics;
}
