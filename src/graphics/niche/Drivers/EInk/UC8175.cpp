#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "./UC8175.h"

#include <cstring>

#include "SPILock.h"

using namespace NicheGraphics::Drivers;

UC8175::UC8175(uint16_t width, uint16_t height, UpdateTypes supported) : EInk(width, height, supported)
{
    bufferRowSize = ((width - 1) / 8) + 1;
    bufferSize = bufferRowSize * height;
}

void UC8175::begin(SPIClass *spi, uint8_t pin_dc, uint8_t pin_cs, uint8_t pin_busy, uint8_t pin_rst)
{
    this->spi = spi;
    this->pin_dc = pin_dc;
    this->pin_cs = pin_cs;
    this->pin_busy = pin_busy;
    this->pin_rst = pin_rst;

    pinMode(pin_dc, OUTPUT);
    pinMode(pin_cs, OUTPUT);
    pinMode(pin_busy, INPUT);

    // Reset is active LOW, hold HIGH when idle.
    if (pin_rst != (uint8_t)-1) {
        pinMode(pin_rst, OUTPUT);
        digitalWrite(pin_rst, HIGH);
    }

    if (!previousBuffer) {
        previousBuffer = new uint8_t[bufferSize];
        if (previousBuffer)
            memset(previousBuffer, 0xFF, bufferSize);
    }
}

void UC8175::update(uint8_t *imageData, UpdateTypes type)
{
    buffer = imageData;
    updateType = (type == UpdateTypes::UNSPECIFIED) ? UpdateTypes::FULL : type;

    if (updateType == FAST && hasPreviousBuffer && previousBuffer && memcmp(previousBuffer, buffer, bufferSize) == 0)
        return;

    reset();
    configCommon();

    if (updateType == FAST)
        configFast();
    else
        configFull();

    writeOldImage();
    writeNewImage();
    sendCommand(0x12); // Display refresh.

    if (previousBuffer) {
        memcpy(previousBuffer, buffer, bufferSize);
        hasPreviousBuffer = true;
    }

    detachFromUpdate();
}

void UC8175::wait(uint32_t timeoutMs)
{
    if (failed)
        return;

    uint32_t started = millis();
    while (digitalRead(pin_busy) == BUSY_ACTIVE) {
        if ((millis() - started) > timeoutMs) {
            failed = true;
            break;
        }
        yield();
    }
}

void UC8175::reset()
{
    if (pin_rst != (uint8_t)-1) {
        digitalWrite(pin_rst, LOW);
        delay(20);
        digitalWrite(pin_rst, HIGH);
        delay(20);
    } else {
        sendCommand(0x12); // Software reset.
        delay(10);
    }

    wait(3000);
}

void UC8175::sendCommand(uint8_t command)
{
    if (failed)
        return;

    spiLock->lock();
    spi->beginTransaction(spiSettings);
    digitalWrite(pin_dc, LOW);
    digitalWrite(pin_cs, LOW);
    spi->transfer(command);
    digitalWrite(pin_cs, HIGH);
    digitalWrite(pin_dc, HIGH);
    spi->endTransaction();
    spiLock->unlock();
}

void UC8175::sendData(uint8_t data)
{
    sendData(&data, 1);
}

void UC8175::sendData(const uint8_t *data, uint32_t size)
{
    if (failed)
        return;

    spiLock->lock();
    spi->beginTransaction(spiSettings);
    digitalWrite(pin_dc, HIGH);
    digitalWrite(pin_cs, LOW);

#if defined(ARCH_ESP32)
    spi->transferBytes(data, NULL, size);
#elif defined(ARCH_NRF52)
    spi->transfer(data, NULL, size);
#else
    for (uint32_t i = 0; i < size; ++i)
        spi->transfer(data[i]);
#endif

    digitalWrite(pin_cs, HIGH);
    digitalWrite(pin_dc, HIGH);
    spi->endTransaction();
    spiLock->unlock();
}

void UC8175::powerOn()
{
    sendCommand(0x04);
    wait(2000);
}

void UC8175::powerOff()
{
    sendCommand(0x02); // Power off.
    wait(1500);
}

void UC8175::writeImage(uint8_t command, const uint8_t *image)
{
    sendCommand(command);
    sendData(image, bufferSize);
}

void UC8175::writeOldImage()
{
    if (updateType == FAST && previousBuffer)
        writeImage(0x10, previousBuffer);
    else
        writeImage(0x10, buffer);
}

void UC8175::writeNewImage()
{
    writeImage(0x13, buffer);
}

void UC8175::detachFromUpdate()
{
    switch (updateType) {
    case FAST:
        return beginPolling(50, 400);
    case FULL:
    default:
        return beginPolling(100, 2000);
    }
}

bool UC8175::isUpdateDone()
{
    return digitalRead(pin_busy) != BUSY_ACTIVE;
}

void UC8175::finalizeUpdate()
{
    powerOff();

    if (pin_rst != (uint8_t)-1) {
        sendCommand(0x07); // Deep sleep.
        sendData(0xA5);
    }
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
