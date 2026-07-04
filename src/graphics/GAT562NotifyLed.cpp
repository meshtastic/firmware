#include "configuration.h"

#if defined(GAT562) && defined(GAT562_NOTIFY_NEOPIXEL_PIN)
#include "graphics/GAT562NotifyLed.h"
#include <Arduino.h>

namespace graphics
{

static constexpr uint32_t notifyDurationMs = 6500;
static constexpr uint32_t notifyFrameMs = 25;
static constexpr uint8_t notifyMaxBrightness = 90;
static constexpr uint32_t bootClearMs = 2000;

GAT562NotifyLed &GAT562NotifyLed::instance()
{
    static GAT562NotifyLed inst;
    return inst;
}

GAT562NotifyLed::GAT562NotifyLed()
    : concurrency::OSThread("GAT562NotifyLed"),
      pixel(1, GAT562_NOTIFY_NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800)
{
    pixel.begin();
    pixel.clear();
    pixel.show();
    bootClearUntilMs = millis() + bootClearMs;
    enabled = true;
    setIntervalFromNow(0);
}

void GAT562NotifyLed::triggerMessage()
{
    startedMs = millis();
    active = true;
    enabled = true;
    setIntervalFromNow(0);
}

void GAT562NotifyLed::setWhite(uint8_t level)
{
    pixel.setPixelColor(0, pixel.Color(level, level, level));
    pixel.show();
}

void GAT562NotifyLed::off()
{
    pixel.clear();
    pixel.show();
}

int32_t GAT562NotifyLed::runOnce()
{
    if (!active) {
        if (millis() < bootClearUntilMs) {
            off();
            return 100;
        } else {
            return disable();
        }
    }

    uint32_t elapsed = millis() - startedMs;
    if (elapsed >= notifyDurationMs) {
        active = false;
        off();
        return disable();
    }

    // Two soft white pulses over the notification window.
    uint32_t phase = elapsed % 800;
    uint8_t level = phase < 400 ? (phase * notifyMaxBrightness) / 400
                                : ((800 - phase) * notifyMaxBrightness) / 400;
    setWhite(level);
    return notifyFrameMs;
}

} // namespace graphics
#endif
