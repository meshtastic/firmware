#include "configuration.h"

#include "TDeckProInput.h"
#include "TDeckProKeyboard.h"
#include "input/TouchScreenImpl1.h"

#include <CSE_CST328.h>
#include <Wire.h>

namespace
{
constexpr uint8_t CST3530_ADDRESS = 0x1A;
constexpr size_t CST3530_REPORT_LENGTH = 9;

CSE_CST328 touchPanel(EINK_WIDTH, EINK_HEIGHT, &Wire, CST328_PIN_RST, CST328_PIN_INT);
bool cst3530Detected = false;
volatile bool touchInterruptPending = false;

bool readCst3530Touch(int16_t *x, int16_t *y)
{
    uint8_t buffer[CST3530_REPORT_LENGTH] = {};
    constexpr uint8_t readCommand[] = {0xD0, 0x07, 0x00, 0x00};
    constexpr uint8_t clearCommand[] = {0xD0, 0x00, 0x02, 0xAB};

    Wire.beginTransmission(CST3530_ADDRESS);
    Wire.write(readCommand, sizeof(readCommand));
    if (Wire.endTransmission() != 0)
        return false;

    if (Wire.requestFrom(static_cast<int>(CST3530_ADDRESS), static_cast<int>(sizeof(buffer))) !=
            static_cast<int>(sizeof(buffer)) ||
        Wire.readBytes(buffer, sizeof(buffer)) != static_cast<int>(sizeof(buffer)))
        return false;

    bool validReport = buffer[2] == 0xFF;
    const uint8_t touchPoints = buffer[3] & 0x0F;
    if (validReport && (touchPoints == 0 || touchPoints > 1))
        validReport = false;

    if (validReport) {
        uint16_t checksum = 0x55;
        for (uint8_t index = 4; index < sizeof(buffer); ++index)
            checksum = static_cast<uint16_t>(checksum + buffer[index]);
        const uint16_t reportedChecksum = static_cast<uint16_t>(buffer[0]) |
                                           (static_cast<uint16_t>(buffer[1]) << 8);
        validReport = checksum == reportedChecksum;
    }

    if (validReport && (buffer[8] >> 4) == 0)
        validReport = false;

    if (validReport) {
        const uint16_t rawX = buffer[4] + ((static_cast<uint16_t>(buffer[7]) & 0x0F) << 8);
        const uint16_t rawY = buffer[5] + ((static_cast<uint16_t>(buffer[7]) & 0xF0) << 4);
        if (rawX >= EINK_WIDTH || rawY >= EINK_HEIGHT)
            validReport = false;
        else {
            *x = static_cast<int16_t>(rawX);
            *y = static_cast<int16_t>(rawY);
        }
    }

    Wire.beginTransmission(CST3530_ADDRESS);
    Wire.write(clearCommand, sizeof(clearCommand));
    Wire.endTransmission();
    return validReport;
}

bool readTouch(int16_t *x, int16_t *y)
{
    if (cst3530Detected) {
        if (!touchInterruptPending)
            return false;
        touchInterruptPending = false;
        return readCst3530Touch(x, y);
    }

    if (!touchPanel.getTouches())
        return false;
    *x = touchPanel.getPoint(0).x;
    *y = touchPanel.getPoint(0).y;
    return true;
}

void IRAM_ATTR touchInterruptHandler()
{
    touchInterruptPending = true;
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

void TDeckProInput::begin()
{
    pinMode(CST328_PIN_RST, OUTPUT);
    digitalWrite(CST328_PIN_RST, HIGH);
    delay(20);
    digitalWrite(CST328_PIN_RST, LOW);
    delay(80);
    digitalWrite(CST328_PIN_RST, HIGH);
    delay(20);

    cst3530Detected = probeCst3530();
    if (!cst3530Detected && !touchPanel.begin())
        LOG_WARN("T-Deck-Pro: CST328 touch initialization failed");

    touchScreenImpl1 = new TouchScreenImpl1(EINK_WIDTH, EINK_HEIGHT, readTouch);
    touchScreenImpl1->init();
}

std::unique_ptr<TCA8418KeyboardBase> TDeckProInput::createTca8418Keyboard()
{
    return std::make_unique<TDeckProKeyboard>();
}
