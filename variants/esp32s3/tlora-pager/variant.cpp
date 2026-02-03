#include "variant.h"
#include "ExtensionIOXL9555.hpp"
extern ExtensionIOXL9555 io;

void earlyInitVariant()
{
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    pinMode(KB_INT, INPUT_PULLUP);
    // io expander
    io.begin(Wire, XL9555_SLAVE_ADDRESS0, SDA, SCL);
    io.pinMode(EXPANDS_DRV_EN, OUTPUT);
    io.digitalWrite(EXPANDS_DRV_EN, HIGH);
    io.pinMode(EXPANDS_AMP_EN, OUTPUT);
    io.digitalWrite(EXPANDS_AMP_EN, LOW);
    io.pinMode(EXPANDS_LORA_EN, OUTPUT);
    io.digitalWrite(EXPANDS_LORA_EN, HIGH);
    io.pinMode(EXPANDS_GPS_EN, OUTPUT);
    io.digitalWrite(EXPANDS_GPS_EN, HIGH);
    io.pinMode(EXPANDS_KB_EN, OUTPUT);
    io.digitalWrite(EXPANDS_KB_EN, HIGH);
    io.pinMode(EXPANDS_SD_EN, OUTPUT);
    io.digitalWrite(EXPANDS_SD_EN, HIGH);
    io.pinMode(EXPANDS_GPIO_EN, OUTPUT);
    io.digitalWrite(EXPANDS_GPIO_EN, HIGH);
    io.pinMode(EXPANDS_SD_PULLEN, INPUT);
}