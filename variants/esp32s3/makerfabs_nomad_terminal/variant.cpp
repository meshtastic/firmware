#include "variant.h"
#include "Arduino.h"

void earlyInitVariant()
{
    // LR1121 and SD card share one SPI bus; deselect both before the SD card is probed
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
}
