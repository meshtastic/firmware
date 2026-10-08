#ifdef ELECROW_ThinkNode_M5

#include "configuration.h"
#include <PCA9557.h>

PCA9557 io(0x18, &Wire1);

int m5Ag3352 = 0;

#if !MESHTASTIC_EXCLUDE_GPS
#include "GpioLogic.h"
#include "gps/GPS.h"

// v2.0 IO7 selects the AG3352 acquisition mode; it does not switch its power rails.
class GpsAcquisitionMode : public GpioPin
{
  public:
    void set(bool on) override
    {
        if (m5Ag3352)
                        io.digitalWrite(PCA_PIN_GPS_HIGHT, on ? HIGH : LOW);
    }
};

static void selectGps(bool ag3352)
{
    m5Ag3352 = ag3352;
    pinMode(PIN_GPS_T_SENSE, INPUT);
    pinMode(PIN_GPS_RESET, OUTPUT);
    if (ag3352) {
        pinMode(GPS_SLEEP_INT, OUTPUT);
        digitalWrite(GPS_SLEEP_INT, HIGH);
        io.digitalWrite(PCA_PIN_GPS_HIGHT, HIGH);
        io.pinMode(PCA_PIN_GPS_HIGHT, OUTPUT);
    } else {
        io.pinMode(PCA_PIN_GPS_HIGHT, INPUT);
    }
    digitalWrite(PIN_GPS_RESET, !GPS_RESET_MODE);
    if (ag3352)
        delay(100);
    LOG_INFO("GPS: use %s pin setup, GPS_HIGHT %d", ag3352 ? "AG3352 v2.0" : "L76K", io.digitalRead(PCA_PIN_GPS_HIGHT));
}

bool initGpsVariant(GnssModel_t model)
{
    static uint8_t candidate = 0;
    if (model != GNSS_MODEL_UNKNOWN) {
        selectGps(model == GNSS_MODEL_AG3352 || model == GNSS_MODEL_AG3335);
        return true;
    }
    if (candidate >= 2)
        return false;
    selectGps(candidate++ == 1); // L76K first, so an old board never gets its unknown expander IO7 driven
    return true;
}
#endif

void earlyInitVariant()
{
    Wire1.begin(48, 47);
    io.pinMode(PCA_PIN_EINK_EN, OUTPUT);
    io.pinMode(PCA_PIN_POWER_EN, OUTPUT);
    io.pinMode(PCA_LED_POWER, OUTPUT);
    io.pinMode(PCA_LED_NOTIFICATION, OUTPUT);
    io.pinMode(PCA_LED_ENABLE, OUTPUT);

    io.digitalWrite(PCA_PIN_POWER_EN, HIGH);
    io.digitalWrite(PCA_LED_NOTIFICATION, LOW);
    io.digitalWrite(PCA_LED_ENABLE, LOW);
}

void initVariant()
{
#if !MESHTASTIC_EXCLUDE_GPS
    if (gps)
        new GpioUnaryTransformer(gps->enablePin, new GpsAcquisitionMode());
#endif
}

void variant_shutdown()
{
#if !MESHTASTIC_EXCLUDE_GPS
    if (m5Ag3352)
        io.digitalWrite(PCA_PIN_GPS_HIGHT, LOW);
#endif
    io.digitalWrite(PCA_PIN_POWER_EN, LOW);
}
#endif