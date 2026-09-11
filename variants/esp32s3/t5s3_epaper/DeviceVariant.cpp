#include "variant.h"
#include "configuration.h"
#include "Arduino.h"
#include "pins_arduino.h"
#include "T5S3InputProvider.h"
#include "T5S3UiPolicy.h"
#include "platform/DevicePowerController.h"
#include "platform/DeviceVariant.h"

#include <Wire.h>

void t5s3LateInit();
void t5s3Shutdown();
void t5BacklightSetForcedBySleep(bool forced);
void t5BacklightSetForcedByTimeout(bool forced);
void t5TouchSetForcedByTimeout(bool forced);
void t5BacklightHandleUserInput();
void t5TouchHandleUserInput();

namespace
{
class T5S3PowerController final : public DevicePowerController
{
  public:
    void onLightSleepEnter() override { t5BacklightSetForcedBySleep(true); }
    void onLightSleepExit() override { t5BacklightSetForcedBySleep(false); }
    void onScreenTimeout() override
    {
        t5BacklightSetForcedByTimeout(true);
        t5TouchSetForcedByTimeout(true);
    }
    void onUserInput() override
    {
        t5BacklightHandleUserInput();
        t5TouchHandleUserInput();
    }
    bool usesCustomBq27220Initialization() const override { return true; }
};

class T5S3Variant final : public DeviceVariant
{
  public:
    void earlyInit() override
    {
        pinMode(LORA_CS, OUTPUT);
        digitalWrite(LORA_CS, HIGH);
        pinMode(SDCARD_CS, OUTPUT);
        digitalWrite(SDCARD_CS, HIGH);
        pinMode(BOARD_BL_EN, OUTPUT);
        digitalWrite(BOARD_BL_EN, HIGH);

        // Select GT911 address 0x14 before the I2C scan.
        pinMode(GT911_PIN_RST, OUTPUT);
        digitalWrite(GT911_PIN_RST, LOW);
        pinMode(GT911_PIN_INT, OUTPUT);
        digitalWrite(GT911_PIN_INT, HIGH);
        delay(1);
        digitalWrite(GT911_PIN_RST, HIGH);
        delay(10);
        pinMode(GT911_PIN_INT, INPUT);
    }

    void lateInit() override { t5s3LateInit(); }
    bool recoverI2C() override
    {
        const bool ended = Wire.end();
        const bool started = Wire.begin(I2C_SDA, I2C_SCL);
        if (!ended || !started)
            LOG_ERROR("T5S3 E-Paper: I2C bus recovery failed (end=%d begin=%d)", ended, started);
        return ended && started;
    }
    bool requiresI2CRecovery() const override { return true; }
    DevicePowerController *powerController() override { return &power; }
    DeviceInputProvider *inputProvider() override { return &input; }
    DeviceUiPolicy *uiPolicy() override { return &ui; }
    void shutdown() override { t5s3Shutdown(); }

  private:
    T5S3PowerController power;
    T5S3InputProvider input{
#if defined(T5_S3_EPAPER_PRO_V2)
        true
#else
        false
#endif
    };
    T5S3UiPolicy ui{
#if defined(T5_S3_EPAPER_PRO_V2)
        true
#else
        false
#endif
    };
};
} // namespace

std::unique_ptr<DeviceVariant> createDeviceVariant()
{
    return std::make_unique<T5S3Variant>();
}
