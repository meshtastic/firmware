
#include "GPS.h"

HardwareSerial _serial_gps(GPS_SERIAL_NUM);

GPS gps;

GPS::GPS() : PeriodicTask(30 * 1000)
{
}

void GPS::setup()
{
#ifdef GPS_RX_PIN
    _serial_gps.begin(GPS_BAUDRATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
#endif
}

void GPS::loop()
{
    PeriodicTask::loop();

#ifdef GPX_RX_PIN
    while (_serial_gps.available())
    {
        _gps.encode(_serial_gps.read());
    }
#endif
}

void GPS::doTask()
{
}

String GPS::getTime()
{
    static char t[12]; // used to sprintf for Serial output

    snprintf(t, sizeof(t), "%02d:%02d:%02d", time.hour(), time.minute(), time.second());
    return t;
}

