#if defined(Nodara)
#include "McuDataInput.h"

#include "Arduino.h"
#include "MessageStore.h"
#include "Nodara.h"
#include "GpioteOverride.h"
#include "SixKeyNavInput.h"
#include "main.h"
#include "mesh/NodeDB.h"
#include "mesh/RadioInterface.h"
#include "mesh/RadioLibInterface.h"
#include "mesh/Throttle.h"
#include "sleep.h"
#include "target_specific.h"
#include "freertosinc.h"

#include <cstdio>
#include <graphics/draw/UIRenderer.h>
#include <OLEDDisplay.h>

namespace nodara
{
namespace
{
#ifndef MCU_DATA_BAUD
#define MCU_DATA_BAUD 1200
#endif

#if defined(MCU_DATA_SERIAL_8E1)
constexpr uint16_t kMcuDataSerialConfig = SERIAL_8E1;
constexpr const char *kMcuDataSerialConfigName = "8E1";
#else
constexpr uint16_t kMcuDataSerialConfig = SERIAL_8N1;
constexpr const char *kMcuDataSerialConfigName = "8N1";
#endif

#if defined(Nodara) && defined(PIN_SERIAL2_RX) && defined(PIN_SERIAL2_TX)
#define MCU_DATA_SERIAL_ENABLED 1
#else
#define MCU_DATA_SERIAL_ENABLED 0
#endif

#ifndef DELAY_FOREVER
#define DELAY_FOREVER portMAX_DELAY
#endif

constexpr uint32_t kMcuDataBaud = MCU_DATA_BAUD;
constexpr uint32_t kMcuDataFrameGapMs = 200;
constexpr uint32_t kLedBlinkMs = 50;
constexpr size_t kMcuDataMaxFrameSize = 32;
constexpr uint8_t kMcuDataAddress = 0x01;
constexpr uint16_t kMcuDataRegister = 0x1000;
constexpr uint16_t kMcuDataCommandShutdownDelayed = 0x0003;
constexpr uint16_t kMcuDataCommandPower = 0x0005;
constexpr uint16_t kMcuDataPowerOnValue = 0x0001;
constexpr uint16_t kMcuDataPowerOffValue = 0x0000;
constexpr size_t kMcuDataWriteSingleFrameSize = 8;
constexpr size_t kMcuDataWriteMultipleFrameSize = 13;

enum class McuParsedCommand {
    InvalidCrc,
    Unsupported,
    PowerOnConfirm,
    ShutdownDelayed,
    ShutdownNow,
};

uint8_t frame[kMcuDataMaxFrameSize] = {};
size_t frameSize = 0;
uint32_t lastByteMs = 0;
bool powerOnConfirmed = false;

uint16_t readBe16(const uint8_t *data)
{
    return (static_cast<uint16_t>(data[0]) << 8) | data[1];
}

uint16_t modbusCrc16(const uint8_t *data, size_t size)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if ((crc & 0x0001) != 0)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }

    return crc;
}

bool hasValidModbusCrc(const uint8_t *data, size_t size)
{
    if (size < 4)
        return false;

    const uint16_t expected = modbusCrc16(data, size - 2);
    const uint16_t received = static_cast<uint16_t>(data[size - 2]) | (static_cast<uint16_t>(data[size - 1]) << 8);
    return expected == received;
}

void logMcuDataFrame(const uint8_t *data, size_t size)
{
    char hex[kMcuDataMaxFrameSize * 3] = {};
    size_t offset = 0;

    for (size_t i = 0; i < size && offset < sizeof(hex); i++) {
        const int written = snprintf(hex + offset, sizeof(hex) - offset, "%02X%s", data[i], i + 1 < size ? " " : "");
        if (written <= 0)
            break;
        offset += static_cast<size_t>(written);
    }

    LOG_INFO("MCU_DATA RX (%u bytes): %s", static_cast<unsigned>(size), hex);
}

McuParsedCommand resolveMcuCommand(uint16_t command, uint16_t value)
{
    if (command == kMcuDataCommandShutdownDelayed)
        return McuParsedCommand::ShutdownDelayed;

    if (command != kMcuDataCommandPower) {
        LOG_WARN("MCU_DATA unknown command value 0x%04X", command);
        return McuParsedCommand::Unsupported;
    }

    if (value == kMcuDataPowerOnValue)
        return McuParsedCommand::PowerOnConfirm;
    if (value == kMcuDataPowerOffValue)
        return McuParsedCommand::ShutdownNow;

    LOG_WARN("MCU_DATA unknown 0x0005 parameter 0x%04X", value);
    return McuParsedCommand::Unsupported;
}

McuParsedCommand parseMcuDataPayload(const uint8_t *data, size_t size)
{
    if (size == kMcuDataWriteSingleFrameSize && data[0] == kMcuDataAddress && data[1] == 0x06 &&
        readBe16(&data[2]) == kMcuDataRegister)
        return resolveMcuCommand(readBe16(&data[4]), kMcuDataPowerOffValue);

    if (size == kMcuDataWriteMultipleFrameSize && data[0] == kMcuDataAddress && data[1] == 0x10 &&
        readBe16(&data[2]) == kMcuDataRegister && readBe16(&data[4]) == 0x0002 && data[6] == 0x04)
        return resolveMcuCommand(readBe16(&data[7]), readBe16(&data[9]));

    return McuParsedCommand::Unsupported;
}

McuParsedCommand parseMcuDataFrame(const uint8_t *data, size_t size)
{
    bool sawValidCrc = false;

    for (size_t offset = 0; offset < size; offset++) {
        if (data[offset] != kMcuDataAddress)
            continue;
        if (offset + 1 >= size)
            break;

        const uint8_t func = data[offset + 1];
        if (func != 0x06 && func != 0x10)
            continue;

        const size_t frameLen = (func == 0x06) ? kMcuDataWriteSingleFrameSize : kMcuDataWriteMultipleFrameSize;
        if (offset + frameLen > size)
            continue;

        if (!hasValidModbusCrc(data + offset, frameLen))
            continue;

        sawValidCrc = true;
        const McuParsedCommand cmd = parseMcuDataPayload(data + offset, frameLen);
        if (cmd == McuParsedCommand::Unsupported)
            continue;

        if (offset > 0)
            LOG_DEBUG("MCU_DATA resynced frame at offset %u", static_cast<unsigned>(offset));

        return cmd;
    }

    return sawValidCrc ? McuParsedCommand::Unsupported : McuParsedCommand::InvalidCrc;
}

void readSerialIntoFrame()
{
#if MCU_DATA_SERIAL_ENABLED
    while (Serial2.available() > 0) {
        const int value = Serial2.read();
        if (value < 0)
            break;

        if (frameSize >= sizeof(frame)) {
            logMcuDataFrame(frame, frameSize);
            frameSize = 0;
        }

        frame[frameSize++] = static_cast<uint8_t>(value);
        lastByteMs = millis();
    }
#endif
}

void blinkPowerOffLed(const uint8_t pin = PIN_LED2, const uint32_t blinkCount = 4, const uint32_t blinkMs = kLedBlinkMs)
{
#ifdef PIN_LED2
    for (uint8_t i = 0; i < blinkCount; i++) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LED_STATE_ON);
        delay(blinkMs);
        digitalWrite(pin, LED_STATE_OFF);
        delay(blinkMs);
    }
#endif
}
} // namespace

void McuDataInput::setup()
{
#if MCU_DATA_SERIAL_ENABLED
    pinMode(MCU_DATA_PIN, INPUT_PULLUP);
    Serial2.begin(kMcuDataBaud, kMcuDataSerialConfig);
    LOG_INFO("MCU_DATA serial RX initialized on pin %u at %u baud %s", static_cast<unsigned>(MCU_DATA_PIN),
             static_cast<unsigned>(kMcuDataBaud), kMcuDataSerialConfigName);
#endif
}

bool onPowerOn = false;

void McuDataInput::processCompletedFrame()
{
    if (frameSize == 0 || Throttle::isWithinTimespanMs(lastByteMs, kMcuDataFrameGapMs))
        return;

    logMcuDataFrame(frame, frameSize);
    const McuParsedCommand parsed = parseMcuDataFrame(frame, frameSize);
    frameSize = 0;
    lastByteMs = 0;

    switch (parsed) {
    case McuParsedCommand::InvalidCrc:
        LOG_WARN("MCU_DATA invalid CRC");
        break;
    case McuParsedCommand::Unsupported:
        LOG_WARN("MCU_DATA unsupported Modbus frame");
        break;
    case McuParsedCommand::PowerOnConfirm:
        handlePowerOnConfirm();
        onPowerOn = true;
        break;
    case McuParsedCommand::ShutdownDelayed:
        handleCommand03();
        break;
    case McuParsedCommand::ShutdownNow:
        onPowerOn = false;
        handleCommand05();
        break;
    }
}

void McuDataInput::loop()
{
#if MCU_DATA_SERIAL_ENABLED
    if (isExternalPowerOff())
        return;
    readSerialIntoFrame();
    processCompletedFrame();
#endif
}

void McuDataInput::handlePowerOnConfirm()
{
    if (powerOnConfirmed) {
        LOG_DEBUG("MCU_DATA duplicate power-on confirm ignored");
        return;
    }

    powerOnConfirmed = true;
    LOG_INFO("MCU_DATA power-on confirmed (0x0005/0x0001)");
    // blinkPowerOffLed(PIN_LED2, 4, kLedBlinkMs);
}

void McuDataInput::handleCommand03()
{
    LOG_INFO("MCU_DATA command 0x0003 received");
    shutdownAtMsec = millis() + DEFAULT_SHUTDOWN_SECONDS * 1000;
    // blinkPowerOffLed(PIN_LED2, 4, kLedBlinkMs);
}

void McuDataInput::handleCommand05()
{
    screen->showSimpleBanner("Power Off", 3000);
    if (isExternalPowerOff())
        return;

    setExternalPowerOff(true);
    LOG_INFO("MCU_DATA command 0x0005/0x0000 — external power-off quiesce");

    if (nodeDB)
        nodeDB->saveToDisk();

    if (sixKeyNavInput)
        sixKeyNavInput->disable();
    unregisterGpiotePortPin(PIN_BUTTON_LEFT);
    unregisterGpiotePortPin(PIN_BUTTON_RIGHT);
    unregisterGpiotePortPin(PIN_BUTTON_UP);
    unregisterGpiotePortPin(PIN_BUTTON_DOWN);
    unregisterGpiotePortPin(PIN_BUTTON_A);
    unregisterGpiotePortPin(PIN_BUTTON_B);

    if (RadioLibInterface::instance)
        static_cast<RadioInterface *>(RadioLibInterface::instance)->disable();

    setBluetoothEnable(false);
    notifyDeepSleep.notifyObservers(NULL);
#if HAS_SCREEN
    messageStore.saveToFlash();

    if (screen) {
        screen->startAlert([](OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
            EINK_ADD_FRAMEFLAG(display, COSMETIC);
            EINK_ADD_FRAMEFLAG(display, BLOCKING);
            graphics::UIRenderer::drawIconScreen("Power Off", display, state, x, y);
        });
        screen->forceDisplay(true);
    }
#endif

    cpuDeepSleep(DELAY_FOREVER);
}
} // namespace nodara
#endif