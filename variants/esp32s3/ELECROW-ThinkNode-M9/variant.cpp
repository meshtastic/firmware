#include "variant.h"
#include "Arduino.h"
#include "SPILock.h"
#include "Wire.h"

void earlyInitVariant()
{
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    delay(100);
}

void lateInitVariant()
{
    // configure keyboard long-press time
    const uint16_t ms = 700;
    concurrency::LockGuard g(spiLock);
    Wire.beginTransmission(0x6C);
    Wire.write(0x03);
    Wire.write((ms >> 8) & 0xFF);
    Wire.write(ms & 0xFF);
    Wire.endTransmission();
}

void variant_shutdown()
{
    uint64_t gpioMask = (1ULL << KB_INT);
    gpio_pulldown_en((gpio_num_t)KB_INT);
    esp_sleep_enable_ext1_wakeup(gpioMask, ESP_EXT1_WAKEUP_ANY_HIGH);
}