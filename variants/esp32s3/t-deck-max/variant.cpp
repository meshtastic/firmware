#include "variant.h"
#include "ExtensionIOXL9555.hpp"

extern ExtensionIOXL9555 io;

static void setExpandPin(uint8_t pin, uint8_t value)
{
    io.pinMode(pin, OUTPUT);
    io.digitalWrite(pin, value);
}

static void pulseExpandPinLow(uint8_t pin, uint32_t lowMs, uint32_t highMs)
{
    setExpandPin(pin, LOW);
    delay(lowMs);
    io.digitalWrite(pin, HIGH);
    delay(highMs);
}

void earlyInitVariant()
{
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
    pinMode(PIN_EINK_CS, OUTPUT);
    digitalWrite(PIN_EINK_CS, HIGH);
    pinMode(KB_INT, INPUT_PULLUP);
    pinMode(CST328_PIN_INT, INPUT_PULLUP);
    pinMode(PIN_EINK_BL, OUTPUT);
    analogWrite(PIN_EINK_BL, 0);

    io.begin(Wire, XL9555_SLAVE_ADDRESS0, SDA, SCL);
    setExpandPin(EXPANDS_MODEM_EN, LOW);
    setExpandPin(EXPANDS_MODEM_PWRKEY, LOW);
    setExpandPin(EXPANDS_LORA_EN, HIGH);
    setExpandPin(EXPANDS_LORA_SEL, HIGH);
    setExpandPin(EXPANDS_GPS_EN, HIGH);
    setExpandPin(EXPANDS_1V8_EN, HIGH);
    setExpandPin(EXPANDS_DRV_EN, HIGH);
    setExpandPin(EXPANDS_AMP_EN, LOW);
    setExpandPin(EXPANDS_AUDIO_SEL, LOW);
    pulseExpandPinLow(EXPANDS_TOUCH_RST, 20, 60);
    pulseExpandPinLow(EXPANDS_KB_RST, 20, 60);
}
