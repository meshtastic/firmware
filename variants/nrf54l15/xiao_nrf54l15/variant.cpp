#include "variant.h"
#include "nrf.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[] = {
    // D0..D5 (P1 header pins)
    36, 37, 38, 39, 42, 43,
    // D6..D10 (P2 header pins): TX, RX, SCK, MISO, MOSI
    72, 71, 65, 68, 66,
    // 11..15: P0.03, P0.04, P2.10, P2.09, P2.06
    3, 4, 74, 73, 70,
    // 16 LED P2.00, 17 button P0.00, 18 SAMD11 RX (nRF TX) P1.09, 19 SAMD11 TX (nRF RX) P1.08
    64, 0, 41, 40,
    // 20 IMU/mic power P0.01, 21 RF switch power P2.03, 22 RF path select P2.05, 23 VBAT divider enable P1.15
    1, 67, 69, 47};

void initVariant()
{
    pinMode(PIN_LED1, OUTPUT);
    ledOff(PIN_LED1);
}
