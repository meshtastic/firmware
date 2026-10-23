#include "variant.h"
#include "nrf.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

// Arduino pin index == physical GPIO number: P0.n = n, P1.n = 32 + n, P2.n = 64 + n
const uint32_t g_ADigitalPinMap[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                                     20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
                                     40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
                                     60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
                                     80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95};

void initVariant()
{
    pinMode(PIN_LED1, OUTPUT);
    ledOff(PIN_LED1);
    pinMode(PIN_LED2, OUTPUT);
    ledOff(PIN_LED2);

    // The core's Uart::begin() and SPIClass::begin() route the signals on the peripheral but leave the
    // pads at their reset config: on nRF54L the UARTE TXD pad stays an input and never drives the pin,
    // and the RXD / MISO pads keep their input buffer disconnected. Configure them here until the core does.
    pinMode(PIN_SERIAL1_TX, OUTPUT);
    digitalWrite(PIN_SERIAL1_TX, HIGH);
    pinMode(PIN_SERIAL1_RX, INPUT);
    pinMode(PIN_SPI_MISO, INPUT);
}
