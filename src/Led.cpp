#include "Led.h"
#include "PowerMon.h"
#include "main.h"
#include "power.h"

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
            PMU->setChargingLedMode(value ? XPOWERS_CHG_LED_ON : XPOWERS_CHG_LED_OFF);
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

#ifdef USE_POWERMON
/**
 * We monitor changes to the LED drive output because we use that as a sanity test in our power monitor stuff.
 */
class MonitoredLedPin : public GpioPin
{
  public:
    void set(bool value)
    {
        if (value)
            powerMon->setState(meshtastic_PowerMon_State_LED_On);
        else
            powerMon->clearState(meshtastic_PowerMon_State_LED_On);
        ledHwPin.set(value);
    }
} monitoredLedPin;
#else
static GpioPin &monitoredLedPin = ledHwPin;
#endif

static GpioBinaryTransformer ledForcer(&ledForceOn, &ledBlink, &monitoredLedPin, GpioBinaryTransformer::Or);