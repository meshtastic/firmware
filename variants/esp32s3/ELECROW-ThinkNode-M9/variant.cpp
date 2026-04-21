#include "configuration.h"

void earlyInitVariant()
{
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
}