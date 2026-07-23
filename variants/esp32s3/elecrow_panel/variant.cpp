// meshtastic/firmware/variants/elecrow_panel/variant.cpp

#include "variant.h"
#include "Arduino.h"
#include "Wire.h"

bool elecrow_v2 = false; // false = v1, true = v2

extern "C" {

void initVariant()
{
    Wire.begin(I2C_SDA, I2C_SCL, 100000);
    delay(50);
    Wire.beginTransmission(0x30);
    if (Wire.endTransmission() == 0) {
        elecrow_v2 = true;
    }
    Wire.end();
}
}