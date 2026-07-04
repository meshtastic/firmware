#pragma once

#include "configuration.h"

#if defined(GAT562) && defined(GAT562_NOTIFY_NEOPIXEL_PIN)
#include "concurrency/OSThread.h"
#include <Adafruit_NeoPixel.h>

namespace graphics
{

class GAT562NotifyLed : private concurrency::OSThread
{
  public:
    static GAT562NotifyLed &instance();

    void triggerMessage();

  protected:
    int32_t runOnce() override;

  private:
    GAT562NotifyLed();
    void setWhite(uint8_t level);
    void off();

    Adafruit_NeoPixel pixel;
    uint32_t startedMs = 0;
    uint32_t bootClearUntilMs = 0;
    bool active = false;
};

} // namespace graphics
#endif
