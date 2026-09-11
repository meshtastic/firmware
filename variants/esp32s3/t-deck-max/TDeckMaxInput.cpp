#include "configuration.h"

#include "TDeckMaxInput.h"
#include "TDeckMaxBoard.h"
#include "TDeckMaxTouch.h"
#include "input/InputBroker.h"
#include "TDeckProKeyboard.h"
#include "input/TouchScreenImpl1.h"

#include <Arduino.h>
#include <CSE_CST328.h>
#include <Wire.h>

namespace
{
constexpr uint8_t CST3530_ADDRESS = CST328_I2C_ADDR;
constexpr size_t CST3530_REPORT_LENGTH = 9;

CSE_CST328 touchPanel(EINK_WIDTH, EINK_HEIGHT, &Wire, CST328_PIN_RST, CST328_PIN_INT);
bool cst3530Detected = false;
volatile bool touchInterruptPending = false;
Observable<const InputEvent *> maxTouchKeySource;
bool maxTouchKeyPressed[3] = {};

void IRAM_ATTR touchInterruptHandler()
{
    touchInterruptPending = true;
}

input_broker_event maxTouchKeyEvent(uint8_t keyId)
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

void publishTouchKey(const t_deck_max::TouchReport &report)
{
    if (report.kind != t_deck_max::TouchReportKind::Key || report.keyId >= 3)
        return;

    const bool wasPressed = maxTouchKeyPressed[report.keyId];
    maxTouchKeyPressed[report.keyId] = report.pressed;
    if (!report.pressed || wasPressed)
        return;

    InputEvent event = {};
    event.source = t_deck_max::MAX_TOUCH_KEY_SOURCE;
    event.inputEvent = maxTouchKeyEvent(report.keyId);
    if (event.inputEvent != INPUT_BROKER_NONE)
        maxTouchKeySource.notifyObservers(&event);
}

bool readCst3530Touch(int16_t *x, int16_t *y)
{
    constexpr uint8_t readCommand[] = {0xD0, 0x07, 0x00, 0x00};
    constexpr uint8_t clearCommand[] = {0xD0, 0x00, 0x02, 0xAB};
    uint8_t buffer[CST3530_REPORT_LENGTH] = {};

    if (!touchInterruptPending)
        return false;
    touchInterruptPending = false;

    t_deck_max::TouchReport report{};
    Wire.beginTransmission(CST3530_ADDRESS);
    Wire.write(readCommand, sizeof(readCommand));
    if (Wire.endTransmission() == 0 &&
        Wire.requestFrom(static_cast<int>(CST3530_ADDRESS), static_cast<int>(sizeof(buffer))) ==
            static_cast<int>(sizeof(buffer)) &&
        Wire.readBytes(buffer, sizeof(buffer)) == static_cast<int>(sizeof(buffer)))
        report = t_deck_max::decodeTouchReport(buffer, sizeof(buffer), EINK_WIDTH, EINK_HEIGHT);

    Wire.beginTransmission(CST3530_ADDRESS);
    Wire.write(clearCommand, sizeof(clearCommand));
    Wire.endTransmission();

    if (report.kind == t_deck_max::TouchReportKind::Key) {
        publishTouchKey(report);
        return false;
    }
    if (report.kind != t_deck_max::TouchReportKind::Coordinate)
        return false;

    *x = static_cast<int16_t>(report.x);
    *y = static_cast<int16_t>(report.y);
    return true;
}

bool readTouch(int16_t *x, int16_t *y)
{
    if (cst3530Detected)
        return readCst3530Touch(x, y);
    if (!touchPanel.getTouches())
        return false;
    *x = touchPanel.getPoint(0).x;
    *y = touchPanel.getPoint(0).y;
    return true;
}

bool probeCst3530()
{
    constexpr uint8_t readCommand[] = {0xD0, 0x03, 0x00, 0x00};
    constexpr uint8_t recoverCommand[] = {0xD0, 0x00, 0x04, 0x00};
    uint8_t buffer[7] = {};

    for (uint8_t retry = 0; retry < 5; ++retry) {
        Wire.beginTransmission(CST3530_ADDRESS);
        Wire.write(readCommand, sizeof(readCommand));
        if (Wire.endTransmission() == 0 && Wire.requestFrom(static_cast<int>(CST3530_ADDRESS), 7) == 7 &&
            Wire.readBytes(buffer, sizeof(buffer)) == static_cast<int>(sizeof(buffer)) && buffer[2] == 0xCA &&
            buffer[3] == 0xCA) {
            pinMode(CST328_PIN_INT, INPUT_PULLUP);
            attachInterrupt(digitalPinToInterrupt(CST328_PIN_INT), touchInterruptHandler, FALLING);
            return true;
        }

        Wire.beginTransmission(CST3530_ADDRESS);
        Wire.write(recoverCommand, sizeof(recoverCommand));
        Wire.endTransmission();
        delay(50);
    }
    return false;
}
} // namespace

void TDeckMaxInput::begin()
{
    tDeckMaxResetTouch();
    touchInterruptPending = false;
    for (bool &pressed : maxTouchKeyPressed)
        pressed = false;

    cst3530Detected = probeCst3530();
    if (!cst3530Detected && !touchPanel.begin())
        LOG_WARN("T-Deck-MAX: CST328 touch initialization failed");

    if (cst3530Detected && inputBroker)
        inputBroker->registerSource(&maxTouchKeySource);

    touchScreenImpl1 = new TouchScreenImpl1(EINK_WIDTH, EINK_HEIGHT, readTouch);
    touchScreenImpl1->init();
}

std::unique_ptr<TCA8418KeyboardBase> TDeckMaxInput::createTca8418Keyboard()
{
    return std::make_unique<TDeckProKeyboard>();
}

uint8_t TDeckMaxInput::tca8418KeyboardAddress() const
{
    return t_deck_max::TCA8418_ADDRESS;
}

bool TDeckMaxInput::isMaxTouchKeySource(const char *source) const
{
    return t_deck_max::isMaxTouchKeySource(source);
}

bool TDeckMaxInput::isSafeMenuBackLabel(const char *label) const
{
    return t_deck_max::isSafeMaxMenuBackLabel(label);
}
