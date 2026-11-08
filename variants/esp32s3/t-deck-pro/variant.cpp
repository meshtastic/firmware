#include "Arduino.h"
#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD
#include "NodeDB.h"
#include "concurrency/Periodic.h"
#include "graphics/niche/Drivers/Backlight/LatchingBacklight.h"
#include "graphics/niche/InkHUD/InkHUD.h"
#include "modules/ExternalNotificationModule.h"
#include <Adafruit_DRV2605.h>
#include <Wire.h>

static Adafruit_DRV2605 haptic;
static bool hapticReady = false;
static bool backlightsEnabled = false;
static concurrency::Periodic *touchFeedbackTimer = nullptr;

static int32_t stopTouchFeedback()
{
    tDeckProSetVibration(PIN_VIBRATION, false);
    return touchFeedbackTimer->disable();
}

bool tDeckProIsV11()
{
    static const bool v11 = []() {
        pinMode(PIN_VIBRATION, OUTPUT);
        digitalWrite(PIN_VIBRATION, HIGH);
        delay(10);
        int deviceId = -1;
        for (uint8_t attempt = 0; attempt < 3; ++attempt) {
            Wire.beginTransmission(DRV2605_ADDR);
            if (Wire.endTransmission() == 0) {
                Wire.beginTransmission(DRV2605_ADDR);
                Wire.write(DRV2605_REG_STATUS);
                if (Wire.endTransmission(false) == 0 && Wire.requestFrom(uint8_t(DRV2605_ADDR), size_t(1)) == 1) {
                    deviceId = Wire.read() >> 5;
                    break;
                }
            }
            if (attempt < 2)
                delay(10);
        }
        const bool found = deviceId == 3 || deviceId == 7;
        if (found) {
            hapticReady = haptic.begin();
            if (hapticReady) {
                haptic.selectLibrary(1);
                haptic.setMode(DRV2605_MODE_INTTRIG);
                haptic.setWaveform(0, 16);
                haptic.setWaveform(1, 0);
            } else {
                LOG_WARN("DRV2605 initialization failed");
            }
        } else {
            digitalWrite(PIN_VIBRATION, LOW);
        }
        LOG_INFO("T-Deck Pro revision: %s", found ? "V1.1" : "V1.0");
        return found;
    }();
    return v11;
}

void tDeckProUpdateBacklights(bool sleeping)
{
    const bool on = backlightsEnabled && !sleeping && !NicheGraphics::InkHUD::InkHUD::getInstance()->isTouchTimedOut();
    digitalWrite(KB_BL_PIN, on);
    if (tDeckProIsV11()) {
        auto *backlight = NicheGraphics::Drivers::LatchingBacklight::getInstance();
        if (backlight->isOn() == on)
            return;
        if (on)
            backlight->latch();
        else
            backlight->off();
    }
}

void tDeckProToggleBacklights()
{
    backlightsEnabled = !backlightsEnabled;
    tDeckProUpdateBacklights();
}

void tDeckProSetVibration(int pin, bool on)
{
    if (touchFeedbackTimer)
        touchFeedbackTimer->disable();
    if (tDeckProIsV11()) {
        if (hapticReady) {
            if (on) {
                haptic.setWaveform(0, 16);
                haptic.go();
            } else
                haptic.stop();
        }
        if (pin == PIN_VIBRATION)
            return;
    }
    if (pin)
        digitalWrite(pin, on);
}

void tDeckProTouchFeedback()
{
    const auto &settings = moduleConfig.external_notification;
    if (!settings.enabled || !(settings.alert_message_vibra || settings.alert_bell_vibra))
        return;
    if (externalNotificationModule && (externalNotificationModule->getMute() || externalNotificationModule->nagging() ||
                                       externalNotificationModule->getExternal(1)))
        return;

    const bool v11 = tDeckProIsV11();
    if ((v11 && !hapticReady) || (!v11 && settings.output_vibra != PIN_VIBRATION))
        return;
    if (!touchFeedbackTimer)
        touchFeedbackTimer = new concurrency::Periodic("TouchHaptic", stopTouchFeedback);

    if (v11) {
        haptic.setWaveform(0, 1);
        haptic.go();
    } else {
        digitalWrite(PIN_VIBRATION, HIGH);
    }
    touchFeedbackTimer->setIntervalFromNow(10);
    touchFeedbackTimer->enabled = true;
}
#endif

void earlyInitVariant()
{
#if defined(MESHTASTIC_INCLUDE_INKHUD) && defined(BOARD_1V8_EN)
    pinMode(BOARD_1V8_EN, OUTPUT);
    digitalWrite(BOARD_1V8_EN, HIGH);
#endif
    pinMode(LORA_EN, OUTPUT);
    digitalWrite(LORA_EN, HIGH);
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
    pinMode(PIN_EINK_CS, OUTPUT);
    digitalWrite(PIN_EINK_CS, HIGH);
}
