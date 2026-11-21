#include "variant.h"
#include "Arduino.h"
#include "nrf.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[] = {
    0xff, 0xff, 2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
    24,   25,   26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
};

void initVariant()
{
    pinMode(PIN_IO_CSA, OUTPUT);
    digitalWrite(PIN_IO_CSA, LOW);
}

void variant_shutdown()
{
    detachInterrupt(PIN_9006_IO1);
    Serial2.end();
#ifdef PIN_GPS_PPS
    nrf_gpio_cfg_default(PIN_GPS_PPS);
    detachInterrupt(PIN_GPS_PPS);
#endif
#ifdef PIN_BUTTON1
    detachInterrupt(PIN_BUTTON1);
#endif
#ifdef PIN_BUTTON2
    detachInterrupt(PIN_BUTTON2);
#endif
    pinMode(PIN_9006_IO1, INPUT_PULLDOWN);
    // Arm the opposite level so a held-high IO1 does not immediately wake System OFF.
    const auto sense = digitalRead(PIN_9006_IO1) ? NRF_GPIO_PIN_SENSE_LOW : NRF_GPIO_PIN_SENSE_HIGH;
    nrf_gpio_cfg_sense_input(PIN_9006_IO1, NRF_GPIO_PIN_PULLDOWN, sense);
}
