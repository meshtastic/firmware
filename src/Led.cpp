#include "Led.h"

GpioVirtPin ledForceOn, ledBlink;

#if defined(LED_PIN)

// Most boards have a GPIO for LED control
static GpioHwPin ledRawHwPin(LED_PIN);

#elif defined(HAS_PMU)

/**
 * A GPIO controlled by the PMU
 */
class GpioPmuPin : public GpioPin
{
  public:
    void set(bool value)
    {
        if (pmu_found && PMU) {
            // blink the axp led
            PMU->setChargingLedMode(ledOn ? XPOWERS_CHG_LED_ON : XPOWERS_CHG_LED_OFF);
        }
    }
} ledRawHwPin;

#else
static GpioVirtPin ledRawHwPin; // Dummy pin for no hardware
#endif

#if LED_INVERTED
static GpioVirtPin ledHwPin;
static GpioNotTransformer ledInverter(&ledHwPin, &ledRawHwPin);
#else
static GpioPin &ledHwPin = ledRawHwPin;
#endif

static GpioBinaryTransformer ledForcer(&ledForceOn, &ledBlink, &ledHwPin, GpioBinaryTransformer::Or);