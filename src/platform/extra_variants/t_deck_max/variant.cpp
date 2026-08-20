#include "configuration.h"

#if defined(_VARIANT_T_DECK_MAX)

#include "TDeckMaxBoard.h"
#include "TDeckMaxTouch.h"

#include <Arduino.h>
#include <CSE_CST328.h>
#include <Wire.h>

#include "AudioBoard.h"
#include "audio/A7682Audio.h"
#include "input/InputBroker.h"
#include "input/TouchScreenImpl1.h"

#if defined(HAS_I2S)
DriverPins PinsAudioBoardES8311;
AudioBoard audioCodecBoard(AudioDriverES8311, PinsAudioBoardES8311);
#endif

static constexpr uint8_t CST3530_ADDR = CST328_I2C_ADDR;
static constexpr size_t CST3530_PROBE_LENGTH = 7;
static constexpr size_t CST3530_REPORT_LENGTH = 9;

static CSE_CST328 tsPanel(EINK_WIDTH, EINK_HEIGHT, &Wire, CST328_PIN_RST, CST328_PIN_INT);
static bool isCst3530 = false;
static volatile bool cst3530TouchInterrupt = false;
static Observable<const InputEvent *> maxTouchKeySource;
static bool maxTouchKeyPressed[3] = {};

static void IRAM_ATTR cst3530InterruptHandler()
{
    cst3530TouchInterrupt = true;
}

static bool probeCst3530()
{
    constexpr uint8_t readCommand[] = {0xD0, 0x03, 0x00, 0x00};
    constexpr uint8_t recoverCommand[] = {0xD0, 0x00, 0x04, 0x00};
    uint8_t buffer[CST3530_PROBE_LENGTH] = {};

    for (uint8_t retry = 0; retry < 5; ++retry) {
        Wire.beginTransmission(CST3530_ADDR);
        Wire.write(readCommand, sizeof(readCommand));
        if (Wire.endTransmission() == 0 && Wire.requestFrom(static_cast<int>(CST3530_ADDR), CST3530_PROBE_LENGTH) ==
                                                 static_cast<int>(CST3530_PROBE_LENGTH) &&
            Wire.readBytes(buffer, sizeof(buffer)) == static_cast<int>(sizeof(buffer)) && buffer[2] == 0xCA &&
            buffer[3] == 0xCA) {
            pinMode(CST328_PIN_INT, INPUT_PULLUP);
            attachInterrupt(digitalPinToInterrupt(CST328_PIN_INT), cst3530InterruptHandler, FALLING);
            LOG_INFO("T-Deck-MAX: CST3530 touch detected");
            return true;
        }

        Wire.beginTransmission(CST3530_ADDR);
        Wire.write(recoverCommand, sizeof(recoverCommand));
        Wire.endTransmission();
        delay(50);
    }

    return false;
}

static input_broker_event maxTouchKeyEvent(uint8_t keyId)
{
    switch (t_deck_max::maxTouchKeyForId(keyId)) {
    case t_deck_max::MaxTouchKey::Left:
        return INPUT_BROKER_LEFT;
    case t_deck_max::MaxTouchKey::Center:
        return INPUT_BROKER_SELECT;
    case t_deck_max::MaxTouchKey::Right:
        return INPUT_BROKER_RIGHT;
    default:
        return INPUT_BROKER_NONE;
    }
}

static void handleMaxTouchKey(const t_deck_max::TouchReport &report)
{
    if (report.kind != t_deck_max::TouchReportKind::Key || report.keyId >= 3)
        return;

    const bool wasPressed = maxTouchKeyPressed[report.keyId];
    maxTouchKeyPressed[report.keyId] = report.pressed;
    if (!report.pressed || wasPressed)
        return;

    const input_broker_event inputEvent = maxTouchKeyEvent(report.keyId);
    if (inputEvent == INPUT_BROKER_NONE)
        return;

    InputEvent event = {};
    event.source = t_deck_max::MAX_TOUCH_KEY_SOURCE;
    event.inputEvent = inputEvent;
    if (inputBroker) {
        maxTouchKeySource.notifyObservers(&event);
    } else {
        LOG_WARN("T-Deck-MAX: touch key input broker unavailable");
    }
}

static bool readCst3530Report(t_deck_max::TouchReport *report)
{
    constexpr uint8_t readCommand[] = {0xD0, 0x07, 0x00, 0x00};
    constexpr uint8_t clearCommand[] = {0xD0, 0x00, 0x02, 0xAB};
    uint8_t buffer[CST3530_REPORT_LENGTH] = {};

    *report = {};
    Wire.beginTransmission(CST3530_ADDR);
    Wire.write(readCommand, sizeof(readCommand));
    if (Wire.endTransmission() == 0 && Wire.requestFrom(static_cast<int>(CST3530_ADDR), CST3530_REPORT_LENGTH) ==
                                             static_cast<int>(CST3530_REPORT_LENGTH) &&
        Wire.readBytes(buffer, sizeof(buffer)) == static_cast<int>(sizeof(buffer))) {
        *report = t_deck_max::decodeTouchReport(buffer, sizeof(buffer), EINK_WIDTH, EINK_HEIGHT);
    }

    Wire.beginTransmission(CST3530_ADDR);
    Wire.write(clearCommand, sizeof(clearCommand));
    if (Wire.endTransmission() != 0)
        LOG_DEBUG("T-Deck-MAX: CST3530 clear command failed");

    return report->kind != t_deck_max::TouchReportKind::None;
}

static bool readTouch(int16_t *x, int16_t *y)
{
    if (isCst3530) {
        if (!cst3530TouchInterrupt)
            return false;
        cst3530TouchInterrupt = false;

        t_deck_max::TouchReport report;
        if (!readCst3530Report(&report))
            return false;
        if (report.kind == t_deck_max::TouchReportKind::Key) {
            handleMaxTouchKey(report);
            return false;
        }

        *x = static_cast<int16_t>(report.x);
        *y = static_cast<int16_t>(report.y);
        return true;
    }

    if (!tsPanel.getTouches())
        return false;

    *x = tsPanel.getPoint(0).x;
    *y = tsPanel.getPoint(0).y;
    return true;
}

void lateInitVariant()
{
    tDeckMaxResetTouch();
    cst3530TouchInterrupt = false;
    for (bool &pressed : maxTouchKeyPressed)
        pressed = false;

    isCst3530 = probeCst3530();
    if (!isCst3530 && !tsPanel.begin())
        LOG_WARN("T-Deck-MAX: CST328 touch initialization failed");

    if (isCst3530 && inputBroker)
        inputBroker->registerSource(&maxTouchKeySource);

    touchScreenImpl1 = new TouchScreenImpl1(EINK_WIDTH, EINK_HEIGHT, readTouch);
    touchScreenImpl1->init();

#if defined(HAS_I2S)
    PinsAudioBoardES8311.addI2C(PinFunction::CODEC, Wire);
    PinsAudioBoardES8311.addI2S(PinFunction::CODEC, DAC_I2S_MCLK, DAC_I2S_BCK, DAC_I2S_WS, DAC_I2S_DOUT, DAC_I2S_DIN);

    CodecConfig cfg;
    cfg.input_device = ADC_INPUT_LINE1;
    cfg.output_device = DAC_OUTPUT_ALL;
    cfg.i2s.bits = BIT_LENGTH_16BITS;
    cfg.i2s.rate = RATE_44K;
    audioCodecBoard.begin(cfg);
    audioCodecBoard.setVolume(75);
    tDeckMaxSetAudioRoute(false);
    tDeckMaxSetAmplifier(false);
#endif
}

void variant_shutdown()
{
#if defined(HAS_A7682_AUDIO)
    if (a7682Audio)
        a7682Audio->shutdown();
#endif

    tDeckMaxSetAmplifier(false);
    tDeckMaxSetAudioRoute(false);
    digitalWrite(PIN_EINK_EN, LOW);
    digitalWrite(KB_BL_PIN, LOW);
    digitalWrite(LORA_CS, HIGH);
    digitalWrite(SDCARD_CS, HIGH);
    digitalWrite(PIN_EINK_CS, HIGH);
    tDeckMaxSetSafeState();
}

#endif // _VARIANT_T_DECK_MAX
