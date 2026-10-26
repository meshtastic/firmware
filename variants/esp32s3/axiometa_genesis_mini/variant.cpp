#include "variant.h"
#include <Arduino.h>

// The AX22-0034 panel shares SCK/MOSI with the LR1262 and initialises before the radio, while
// nothing has driven the SX1262's NSS yet - SPI.begin() only records the CS pin, it never
// configures it. A floating NSS lets the ST7735 init sequence clock into the SX1262, and with
// NRST unconnected on the AX22 port only a power cycle clears it. Park NSS high first.
void earlyInitVariant()
{
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
}
