#include "configuration.h"

#ifdef RADIOMASTER_NOMAD_GEMINI

#include "RadioExternalPa.h"
#include <Arduino.h>

static const struct {
    int8_t totalDbm;
    int8_t chipDbm;
    uint8_t apc2;
} NOMAD_PA_CAL[] = {
    {10, -17, 150}, {14, -16, 120}, {17, -14, 120}, {20, -11, 120}, {24, -7, 120}, {27, -3, 120}, {30, 5, 95},
};
static const size_t NOMAD_PA_CAL_N = sizeof(NOMAD_PA_CAL) / sizeof(NOMAD_PA_CAL[0]);

void earlyInitVariant()
{
    pinMode(NOMAD_SECOND_RADIO_NSS_PIN, OUTPUT);
    digitalWrite(NOMAD_SECOND_RADIO_NSS_PIN, HIGH);
    pinMode(NOMAD_SECOND_RADIO_NRESET_PIN, OUTPUT);
    digitalWrite(NOMAD_SECOND_RADIO_NRESET_PIN, LOW);
    pinMode(NOMAD_WIFI_BACKPACK_NRESET_PIN, OUTPUT);
    digitalWrite(NOMAD_WIFI_BACKPACK_NRESET_PIN, LOW);
    dacWrite(RADIO_PA_APC2_PIN, 150);
}

int8_t radioExternalPaMapPower(int8_t requestedTotalDbm, float frequencyMhz)
{
    size_t selected = 0;
    if (frequencyMhz < 1000.0f) {
        for (size_t i = 1; i < NOMAD_PA_CAL_N && requestedTotalDbm >= NOMAD_PA_CAL[i].totalDbm; i++)
            selected = i;
    }
    dacWrite(RADIO_PA_APC2_PIN, NOMAD_PA_CAL[selected].apc2);
    return NOMAD_PA_CAL[selected].chipDbm;
}

#endif // RADIOMASTER_NOMAD_GEMINI
