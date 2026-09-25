#include "variant.h"
#include "Arduino.h"
#include "SPILock.h"
#include "Wire.h"

#define KBD_ADDR_M9_VERSION1_0 0x6C
#define KBD_ADDR_M9_VERSION1_1 0x6D

int m9Version = 0;

void earlyInitVariant()
{
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    pinMode(PIN_GPS_EN, OUTPUT);
    digitalWrite(PIN_GPS_EN, !GPS_EN_ACTIVE);
    pinMode(GPS_RTC_INT, OUTPUT);
    digitalWrite(GPS_RTC_INT, LOW);
    delay(100);

    // After a warm reboot (RTC_SW_CPU_RST) I2C slaves-can be left mid-transaction,
    // holding SDA low. This causes ESP_ERR_INVALID_STATE during the I2C scan,
    // which leaves the I2C peripheral generating spurious interrupt requests.
    // Fix: send 9 SCL clock pulses (enough to clock out any stuck byte) plus a
    // STOP condition on both buses before the Wire driver initialises them.  On
    // a cold boot the SDA line is already high so the loop exits after the first
    // check and the overhead is negligible (~10 µs).
    auto recoverI2C = [](uint8_t sda, uint8_t scl) {
        pinMode(scl, OUTPUT);
        digitalWrite(scl, HIGH);
        pinMode(sda, INPUT_PULLUP);
        delayMicroseconds(5);
        for (int i = 0; i < 9; i++) {
            if (digitalRead(sda))
                break; // SDA released - slave is no longer driving the bus
            digitalWrite(scl, LOW);
            delayMicroseconds(5);
            digitalWrite(scl, HIGH);
            delayMicroseconds(5);
        }
        // STOP condition: SDA goes LOW then HIGH while SCL remains HIGH.
        digitalWrite(scl, HIGH);
        delayMicroseconds(2);
        pinMode(sda, OUTPUT);
        digitalWrite(sda, LOW);
        delayMicroseconds(5);
        digitalWrite(sda, HIGH);
        delayMicroseconds(5);
        // Release both pins so Wire.begin() can claim them as I2C.
        pinMode(scl, INPUT);
        pinMode(sda, INPUT);
    };
    recoverI2C(I2C_SDA, I2C_SCL);
    recoverI2C(I2C_SDA1, I2C_SCL1);
}

void initVariant()
{
    // determine M9 version
    Wire.begin(SDA, SCL);
    Wire.beginTransmission(KBD_ADDR_M9_VERSION1_0);
    if (Wire.endTransmission() == 0) {
        m9Version = 1;
    }
    Wire.beginTransmission(KBD_ADDR_M9_VERSION1_1);
    if (Wire.endTransmission() == 0) {
        m9Version = 2;
    }

    if (m9Version > 0) {
        // configure keyboard long-press time
        const uint16_t ms = 700;
        Wire.beginTransmission(m9Version == 1 ? KBD_ADDR_M9_VERSION1_0 : KBD_ADDR_M9_VERSION1_1);
        Wire.write(0x03);
        Wire.write((ms >> 8) & 0xFF);
        Wire.write(ms & 0xFF);
        Wire.endTransmission();
    }
    Wire.end();
}

void variant_shutdown()
{
    pinMode(PIN_GPS_EN, OUTPUT);
    digitalWrite(PIN_GPS_EN, !GPS_EN_ACTIVE);
    uint64_t gpioMask = (1ULL << KB_INT);
    gpio_pulldown_en((gpio_num_t)KB_INT);
    esp_sleep_enable_ext1_wakeup(gpioMask, ESP_EXT1_WAKEUP_ANY_HIGH);
}