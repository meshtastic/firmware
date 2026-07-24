#include "configuration.h"

#ifdef RADIOMASTER_NOMAD_GEMINI

#include "RadioExternalPa.h"
#include <Arduino.h>

/**
 * RadioMaster Nomad Gemini external analog PA driver.
 *
 * The Nomad's PA is biased by an analog control voltage on the APC2 pin
 * (RADIO_PA_APC2_PIN = GPIO26 = ESP32 DAC channel 2), driven with dacWrite()
 * (0-255 -> 0-3.3V). The DAC is essentially a fixed bias; the actual output power
 * is set by driving the LR1121 chip at a (negative) output power and letting the PA
 * add ~25-31 dB of gain.
 *
 * The calibration below is derived from the ExpressLRS hardware target
 * "TX/Radiomaster Nomad.json" (power_values = APC2 DAC codes, power_values2 = chip
 * output dBm). These values reproduce ELRS behaviour but have NOT been validated on
 * a bench against a power meter for Meshtastic - verify before trusting at 1 W, both
 * for PA safety and for regulatory compliance.
 *
 * TODO: dual-band (Gemini) operation uses ELRS power_values_dual and a second radio;
 * once JANUS_RADIO is implemented, branch on freqHz / active radio here.
 */

// total dBm corresponds to the ELRS power presets 10/25/50/100/250/500/1000 mW.
static const struct {
    int8_t totalDbm; // desired total radiated power
    int8_t chipDbm;  // LR1121 output power register (ELRS power_values2)
    uint8_t apc2;    // APC2 DAC code (ELRS power_values)
} NOMAD_PA_CAL[] = {
    {10, -17, 120}, // 10 mW
    {14, -16, 120}, // 25 mW
    {17, -14, 120}, // 50 mW
    {20, -11, 120}, // 100 mW
    {24, -7, 120},  // 250 mW
    {27, -3, 120},  // 500 mW
    {30, 5, 95},    // 1000 mW (1 W)
};
static const size_t NOMAD_PA_CAL_N = sizeof(NOMAD_PA_CAL) / sizeof(NOMAD_PA_CAL[0]);

// APC2 bias chosen for the currently-configured power level; applied on TX, dropped
// to 0 for RX/idle/sleep so the PA only draws current while transmitting.
static uint8_t nomadApc2Level = 0;

// Runs at the start of setup(), before the LoRa radio is initialized.
void earlyInitVariant()
{
    dacWrite(RADIO_PA_APC2_PIN, 0); // bias the PA off until we actually transmit
    nomadApc2Level = 0;
}

int8_t radioExternalPaMapPower(int8_t requestedTotalDbm, float freqHz)
{
    (void)freqHz; // single-band for now (see dual-band TODO above)

    if (requestedTotalDbm <= NOMAD_PA_CAL[0].totalDbm) {
        nomadApc2Level = NOMAD_PA_CAL[0].apc2;
        return NOMAD_PA_CAL[0].chipDbm;
    }
    const size_t last = NOMAD_PA_CAL_N - 1;
    if (requestedTotalDbm >= NOMAD_PA_CAL[last].totalDbm) {
        nomadApc2Level = NOMAD_PA_CAL[last].apc2;
        return NOMAD_PA_CAL[last].chipDbm;
    }

    // Piecewise-linear interpolation of chip dBm between the bracketing points.
    for (size_t i = 1; i < NOMAD_PA_CAL_N; i++) {
        if (requestedTotalDbm <= NOMAD_PA_CAL[i].totalDbm) {
            const int t0 = NOMAD_PA_CAL[i - 1].totalDbm, t1 = NOMAD_PA_CAL[i].totalDbm;
            const int c0 = NOMAD_PA_CAL[i - 1].chipDbm, c1 = NOMAD_PA_CAL[i].chipDbm;
            const int chip = c0 + ((c1 - c0) * (requestedTotalDbm - t0) + (t1 - t0) / 2) / (t1 - t0); // round-to-nearest
            nomadApc2Level = NOMAD_PA_CAL[i - 1].apc2; // only the top point lowers the bias
            return (int8_t)chip;
        }
    }
    nomadApc2Level = NOMAD_PA_CAL[last].apc2; // unreachable
    return NOMAD_PA_CAL[last].chipDbm;
}

void radioExternalPaTxEnable()
{
    dacWrite(RADIO_PA_APC2_PIN, nomadApc2Level);
}

void radioExternalPaRxIdle()
{
    dacWrite(RADIO_PA_APC2_PIN, 0);
}

void radioExternalPaSleep()
{
    dacWrite(RADIO_PA_APC2_PIN, 0);
}

#endif // RADIOMASTER_NOMAD_GEMINI
