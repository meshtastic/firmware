#include "Led.h"
#include "PowerMon.h"
#include "main.h"
#include "power.h"

GpioVirtPin ledForceOn, ledBlink;

#if defined(LED_PIN)
// Most boards have a GPIO for LED control
static GpioHwPin ledRawHwPin(LED_PIN);
#else
static GpioVirtPin ledRawHwPin; // Dummy pin for no hardware
#endif

#if LED_STATE_ON == 0
static GpioVirtPin ledHwPin;
static GpioNotTransformer ledInverter(&ledHwPin, &ledRawHwPin);
#else
static GpioPin &ledHwPin = ledRawHwPin;
#endif

#if defined(HAS_PMU)
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
} ledPmuHwPin;

// In some cases we need to drive a PMU LED and a normal LED
static GpioSplitter ledFinalPin(&ledHwPin, &ledPmuHwPin);
#else
static GpioPin &ledFinalPin = ledHwPin;
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
        if (powerMon) {
            if (value)
                powerMon->setState(meshtastic_PowerMon_State_LED_On);
            else
                powerMon->clearState(meshtastic_PowerMon_State_LED_On);
        }
        ledFinalPin.set(value);
    }
} monitoredLedPin;
#else
static GpioPin &monitoredLedPin = ledFinalPin;
#endif

static GpioBinaryTransformer ledForcer(&ledForceOn, &ledBlink, &monitoredLedPin, GpioBinaryTransformer::Or);