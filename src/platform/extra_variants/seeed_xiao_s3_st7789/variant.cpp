#include "configuration.h"

#ifdef SEEED_XIAO_S3_ST7789
void earlyInitVariant()
{
    pinMode(ST7789_NSS, OUTPUT);
    digitalWrite(ST7789_NSS, HIGH);
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
}
#endif
