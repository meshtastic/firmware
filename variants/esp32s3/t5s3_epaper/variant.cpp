#include "configuration.h"

#ifdef T5_S3_EPAPER_PRO

void earlyInitVariant()
{
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
    pinMode(BOARD_BL_EN, OUTPUT);

    // Program GT911 touch controller to I2C address 0x14 (GT911_SLAVE_ADDRESS_H) before
    // the I2C bus scan runs.  GPIO3 (INT) defaults LOW on ESP32-S3 cold boot, which would
    // leave the GT911 at 0x5D (GT911_SLAVE_ADDRESS_L) — the same address as the SFA30
    // air quality sensor — causing a false-positive SFA30 detection during the I2C scan.
    //
    // GT911 datasheet §4.3 "Address Selection":
    //   Pull INT HIGH before releasing RST → device latches address 0x14 (SLAVE_ADDRESS_H)
    //   Pull INT LOW  before releasing RST → device latches address 0x5D (SLAVE_ADDRESS_L)
    //   Minimum RST assert time: 100 µs; minimum startup time after RST deassert: 5 ms.
    //
    // lateInitVariant() calls touch.begin() which repeats this sequence internally while
    // also performing full I2C initialisation; the double-reset is harmless.
    pinMode(GT911_PIN_RST, OUTPUT);
    digitalWrite(GT911_PIN_RST, LOW);
    pinMode(GT911_PIN_INT, OUTPUT);
    digitalWrite(GT911_PIN_INT, HIGH); // HIGH → latch address 0x14
    delay(1);                          // > 100 µs
    digitalWrite(GT911_PIN_RST, HIGH);
    delay(10);                     // > 5 ms startup
    pinMode(GT911_PIN_INT, INPUT); // release INT for interrupt use
}

void variant_shutdown()
{
    // Ensure frontlight is off during deep sleep
    digitalWrite(BOARD_BL_EN, LOW);
}

#endif
