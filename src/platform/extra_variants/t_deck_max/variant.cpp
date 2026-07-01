#include "configuration.h"

#ifdef T_DECK_MAX

#include "AudioBoard.h"
#include "ExtensionIOXL9555.hpp"
#include "HynTouch.h"
#include "input/InputBroker.h"
#include "input/TouchScreenImpl1.h"
#include "sleep.h"
#include <Wire.h>

extern ExtensionIOXL9555 io;

DriverPins PinsAudioBoardES8311;
AudioBoard board(AudioDriverES8311, PinsAudioBoardES8311);

static bool readTouch(int16_t *x, int16_t *y)
{
    int16_t xArray[1] = {0};
    int16_t yArray[1] = {0};

    if (hyn_touch_get_point(xArray, yArray, 1) == 0) {
        return false;
    }

    *x = xArray[0];
    *y = yArray[0];
    return true;
}

static input_broker_event touchKeyToEvent(uint8_t keyId)
{
    // Vendor firmware reports the three bezel keys left-to-right as heart, circle, paper airplane.
    switch (keyId) {
    case 0:
        return INPUT_BROKER_USER_PRESS;
    case 1:
        return INPUT_BROKER_SELECT;
    case 2:
        return INPUT_BROKER_SEND_PING;
    default:
        return INPUT_BROKER_NONE;
    }
}

static void touchKeyCallback(uint8_t keyId, bool pressed, void *userData)
{
    (void)userData;
    if (!pressed || !inputBroker) {
        return;
    }

    InputEvent event = {
        .source = "touchkeys",
        .inputEvent = touchKeyToEvent(keyId),
        .kbchar = 0,
        .touchX = 0,
        .touchY = 0,
    };
    if (event.inputEvent != INPUT_BROKER_NONE) {
        inputBroker->injectInputEvent(&event);
        LOG_DEBUG("T-Deck Max touch key %u pressed", keyId);
    }
}

#ifdef ARCH_ESP32
struct TDeckMaxTouchLightSleepObserver {
    int onLightSleep(void *)
    {
        hyn_touch_before_light_sleep();
        return 0;
    }

    int onLightSleepEnd(esp_sleep_wakeup_cause_t)
    {
        hyn_touch_after_light_sleep();
        return 0;
    }

    CallbackObserver<TDeckMaxTouchLightSleepObserver, void *> sleepObserver{this, &TDeckMaxTouchLightSleepObserver::onLightSleep};
    CallbackObserver<TDeckMaxTouchLightSleepObserver, esp_sleep_wakeup_cause_t> wakeObserver{
        this, &TDeckMaxTouchLightSleepObserver::onLightSleepEnd};
} static touchLightSleepObserver;
#endif

void tDeckMaxSetAudioAmp(bool enable)
{
    io.digitalWrite(EXPANDS_AUDIO_SEL, LOW);
    io.digitalWrite(EXPANDS_AMP_EN, enable ? HIGH : LOW);
}

void lateInitVariant()
{
    hyn_touch_attach_xl9555(&io);
    hyn_touch_set_key_callback(touchKeyCallback, nullptr);
    if (hyn_touch_init()) {
#ifdef ARCH_ESP32
        touchLightSleepObserver.sleepObserver.observe(&notifyLightSleep);
        touchLightSleepObserver.wakeObserver.observe(&notifyLightSleepEnd);
#endif
        touchScreenImpl1 = new TouchScreenImpl1(EINK_WIDTH, EINK_HEIGHT, readTouch);
        touchScreenImpl1->init();
    } else {
        LOG_WARN("T-Deck Max touch init failed");
    }

    PinsAudioBoardES8311.addI2C(PinFunction::CODEC, Wire);
    PinsAudioBoardES8311.addI2S(PinFunction::CODEC, DAC_I2S_MCLK, DAC_I2S_BCK, DAC_I2S_WS, DAC_I2S_DOUT, DAC_I2S_DIN);

    CodecConfig cfg;
    cfg.input_device = ADC_INPUT_LINE1;
    cfg.output_device = DAC_OUTPUT_ALL;
    cfg.i2s.bits = BIT_LENGTH_16BITS;
    cfg.i2s.rate = RATE_44K;
    board.begin(cfg);
    board.setVolume(75);
}

#endif
