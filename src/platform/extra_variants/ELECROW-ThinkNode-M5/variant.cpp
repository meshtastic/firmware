#ifdef ELECROW_ThinkNode_M5

#include "configuration.h"
#include <PCA9557.h>

PCA9557 io(0x18, &Wire1);

int m5HasAg3352 = 0;

#if !MESHTASTIC_EXCLUDE_GPS
#include "GpioLogic.h"
#include "gps/GPS.h"

// Follows the GPS enable; GPS::setPowerState() pulses RTC_INT after it to leave RTC sleep.
class GpsCoreRail : public GpioPin
{
  public:
    void set(bool on) override
    {
        digitalWrite(PIN_GPS_0V8_EN, on ? HIGH : LOW);
        if (on)
            delay(100);
    }
};

static void powerUpAg3352()
{
    // RTC_INT must rest low; the wake edge only counts once the core rail is up.
    pinMode(GPS_RTC_INT, OUTPUT);
    digitalWrite(GPS_RTC_INT, LOW);
    pinMode(PIN_GPS_0V8_EN, OUTPUT);
    digitalWrite(PIN_GPS_0V8_EN, LOW);
    delay(10);
    digitalWrite(PIN_GPS_0V8_EN, HIGH);
    delay(100);
    digitalWrite(GPS_RTC_INT, HIGH);
    delay(10);
    digitalWrite(GPS_RTC_INT, LOW);
}
#endif

// No LOG_* here: runs before consoleInit().
void earlyInitVariant()
{
    Wire1.begin(48, 47);
    io.pinMode(PCA_PIN_EINK_EN, OUTPUT);
    io.pinMode(PCA_PIN_POWER_EN, OUTPUT);
    io.pinMode(PCA_LED_POWER, OUTPUT);
    io.pinMode(PCA_LED_NOTIFICATION, OUTPUT);
    io.pinMode(PCA_LED_ENABLE, OUTPUT);
    io.pinMode(PCA_PIN_GPS_VERSION, INPUT);

    io.digitalWrite(PCA_PIN_POWER_EN, HIGH);
    io.digitalWrite(PCA_LED_NOTIFICATION, LOW);
    io.digitalWrite(PCA_LED_ENABLE, LOW);

    delay(2);
    uint8_t high = 0;
    for (uint8_t i = 0; i < 5; i++)
        high += io.digitalRead(PCA_PIN_GPS_VERSION) == HIGH;
    m5HasAg3352 = high >= 3;

#if !MESHTASTIC_EXCLUDE_GPS
    if (m5HasAg3352)
        powerUpAg3352();
#endif
}

void lateInitVariant()
{
    LOG_INFO("ThinkNode M5 %s, GPS %s", m5HasAg3352 ? "v2.0" : "v1.0", m5HasAg3352 ? "AG3352" : "L76K");
#if !MESHTASTIC_EXCLUDE_GPS
    if (gps && m5HasAg3352)
        new GpioUnaryTransformer(gps->enablePin, new GpsCoreRail());
#endif
}

void variant_shutdown()
{
#if !MESHTASTIC_EXCLUDE_GPS
    if (m5HasAg3352) {
        digitalWrite(GPS_RTC_INT, LOW);
        digitalWrite(PIN_GPS_0V8_EN, LOW);
    }
#endif
    io.digitalWrite(PCA_PIN_POWER_EN, LOW);
}
#endif