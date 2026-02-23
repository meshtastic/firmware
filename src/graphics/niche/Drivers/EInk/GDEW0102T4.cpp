#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "./GDEW0102T4.h"

#include "SPILock.h"

using namespace NicheGraphics::Drivers;

GDEW0102T4::GDEW0102T4() : EInk(width, height, supported)
{
    bufferRowSize = ((width - 1) / 8) + 1;
    bufferSize = bufferRowSize * height;
}

void GDEW0102T4::begin(SPIClass *spi, uint8_t pin_dc, uint8_t pin_cs, uint8_t pin_busy, uint8_t pin_rst)
{
    this->spi = spi;
    this->pin_dc = pin_dc;
    this->pin_cs = pin_cs;
    this->pin_busy = pin_busy;
    this->pin_rst = pin_rst;

    pinMode(this->pin_dc, OUTPUT);
    pinMode(this->pin_cs, OUTPUT);
    pinMode(this->pin_busy, INPUT);

    if (this->pin_rst != (uint8_t)-1) {
        pinMode(this->pin_rst, OUTPUT);
        digitalWrite(this->pin_rst, HIGH);
    }

    configDisplay();
}

void GDEW0102T4::update(uint8_t *imageData, UpdateTypes type)
{
    this->buffer = imageData;
    this->updateType = type;

    configDisplay();

    // For this panel, writing white to old-image RAM before full refresh is the
    // most reliable baseline and matches the vendor/GxEPD flow.
    sendCommand(0x10);
    for (uint32_t i = 0; i < bufferSize; ++i)
        sendData((uint8_t)0xFF);

    writeNewImage();

    // Full display refresh (UC8175 family)
    sendCommand(0x12);

    detachFromUpdate();
}

void GDEW0102T4::wait(uint32_t timeoutMs)
{
    uint32_t started = millis();
    while (digitalRead(pin_busy) == BUSY_ACTIVE) {
        if ((millis() - started) > timeoutMs) {
            failed = true;
            break;
        }
        yield();
    }
}

void GDEW0102T4::reset()
{
    if (pin_rst != (uint8_t)-1) {
        digitalWrite(pin_rst, LOW);
        delay(20);
        digitalWrite(pin_rst, HIGH);
        delay(20);
    } else {
        sendCommand(0x12); // software reset
        delay(10);
    }

    wait(1000);
}

void GDEW0102T4::sendCommand(uint8_t command)
{
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

void GDEW0102T4::sendData(uint8_t data)
{
    sendData(&data, 1);
}

void GDEW0102T4::sendData(const uint8_t *data, uint32_t size)
{
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

void GDEW0102T4::configDisplay()
{
    reset();

    // Vendor-proven wake sequence (LilyGO/GxEPD path for GDGDEW0102T4 / UC8175)
    sendCommand(0x00);
    sendData(0x5F);

    sendCommand(0x2A);
    sendData(0x00);
    sendData(0x00);

    sendCommand(0x04); // Power on
    wait(2000);

    sendCommand(0x50);
    sendData(0x97);
}

void GDEW0102T4::writeOldImage()
{
    sendCommand(0x10);
    sendData(buffer, bufferSize);
}

void GDEW0102T4::writeNewImage()
{
    sendCommand(0x13);
    sendData(buffer, bufferSize);
}

void GDEW0102T4::detachFromUpdate()
{
    switch (updateType) {
    case FULL:
        EInk::beginPolling(10, 900);
        break;
    case FAST:
        // This panel path currently uses the same full-refresh command sequence.
        EInk::beginPolling(10, 900);
        break;
    default:
        EInk::beginPolling(10, 900);
        break;
    }
}

bool GDEW0102T4::isUpdateDone()
{
    return digitalRead(pin_busy) != BUSY_ACTIVE;
}

void GDEW0102T4::finalizeUpdate()
{
    // Power-off + deep sleep sequence used by the reference implementation.
    sendCommand(0x02); // Power off
    wait(2000);

    if (pin_rst != (uint8_t)-1) {
        sendCommand(0x07); // Deep sleep
        sendData(0xA5);
    }
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
