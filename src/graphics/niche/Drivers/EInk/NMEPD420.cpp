#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "./NMEPD420.h"

#include <cstring>

#include "SPILock.h"
#include "mesh/Throttle.h"

using namespace NicheGraphics::Drivers;

NMEPD420::NMEPD420() : EInk(WIDTH, HEIGHT, (UpdateTypes)(FULL | FAST)) {}

void NMEPD420::begin(SPIClass *spi, uint8_t pin_dc, uint8_t pin_cs, uint8_t pin_busy, uint8_t pin_rst)
{
    this->spi = spi;
    pinDc = pin_dc;
    pinCs = pin_cs;
    pinBusy = pin_busy;
    pinRst = pin_rst;

    pinMode(pinDc, OUTPUT);
    digitalWrite(pinDc, HIGH);
    pinMode(pinCs, OUTPUT);
    digitalWrite(pinCs, HIGH);
    pinMode(pinBusy, INPUT_PULLUP);

    if (pinRst != (uint8_t)-1) {
        pinMode(pinRst, OUTPUT);
        digitalWrite(pinRst, HIGH);
    }

    detectController();
}

bool NMEPD420::supports(UpdateTypes type)
{
    if (type == FULL)
        return true;
    if (type == FAST)
        return controller == Controller::SSD1683;
    return false;
}

void NMEPD420::detectController()
{
    if (pinRst == (uint8_t)-1) {
        controller = Controller::SSD1683;
        busyActiveLevel = HIGH;
        LOG_WARN("NM-EPD-420 controller detection requires RST; assuming SSD1683");
        return;
    }

    digitalWrite(pinRst, HIGH);
    delay(5);
    digitalWrite(pinRst, LOW);
    delay(10);
    digitalWrite(pinRst, HIGH);
    pinMode(pinBusy, INPUT_PULLUP);

    uint8_t lowCount = 0;
    uint8_t highCount = 0;
    for (uint8_t i = 0; i < 80; ++i) {
        if (digitalRead(pinBusy) == LOW)
            ++lowCount;
        else
            ++highCount;
        delay(1);
    }

    controller = lowCount < 12 ? Controller::UC8179 : Controller::SSD1683;
    busyActiveLevel = controller == Controller::UC8179 ? LOW : HIGH;
    const char *controllerName = controller == Controller::UC8179 ? "UC8179" : "SSD1683";
    LOG_INFO("NM-EPD-420 EPD controller=%s BUSY low=%u high=%u", controllerName, lowCount, highCount);
}

void NMEPD420::update(uint8_t *imageData, UpdateTypes type)
{
    buffer = imageData;
    updateType = type == FAST && supports(FAST) ? FAST : FULL;
    failed = false;

    reset();
    if (controller == Controller::SSD1683)
        updateSsd1683(updateType);
    else
        updateUc8179();
}

void NMEPD420::reset()
{
    if (pinRst != (uint8_t)-1) {
        digitalWrite(pinRst, LOW);
        delay(10);
        digitalWrite(pinRst, HIGH);
        delay(10);
    }

    if (controller == Controller::SSD1683) {
        sendCommand(0x12);
        delay(10);
        waitUntilReady(1000);
    }
}

bool NMEPD420::waitUntilReady(uint32_t timeoutMs, bool markFailure)
{
    uint32_t startedAt = millis();
    while (digitalRead(pinBusy) == busyActiveLevel) {
        if (!Throttle::isWithinTimespanMs(startedAt, timeoutMs)) {
            if (markFailure)
                failed = true;
            return false;
        }
        yield();
    }
    return true;
}

void NMEPD420::sendCommand(uint8_t command)
{
    if (failed)
        return;

    spiLock->lock();
    spi->beginTransaction(spiSettings);
    digitalWrite(pinDc, LOW);
    digitalWrite(pinCs, LOW);
    spi->transfer(command);
    digitalWrite(pinCs, HIGH);
    digitalWrite(pinDc, HIGH);
    spi->endTransaction();
    spiLock->unlock();
}

void NMEPD420::sendData(uint8_t data)
{
    sendData(&data, 1);
}

void NMEPD420::sendData(const uint8_t *data, uint32_t size)
{
    if (failed)
        return;

    spiLock->lock();
    spi->beginTransaction(spiSettings);
    digitalWrite(pinDc, HIGH);
    digitalWrite(pinCs, LOW);
    spi->transferBytes(data, nullptr, size);
    digitalWrite(pinCs, HIGH);
    spi->endTransaction();
    spiLock->unlock();
}

void NMEPD420::sendRepeated(uint8_t value, uint32_t size)
{
    uint8_t block[64];
    memset(block, value, sizeof(block));
    while (size > 0) {
        uint32_t chunk = min(size, (uint32_t)sizeof(block));
        sendData(block, chunk);
        size -= chunk;
    }
}

void NMEPD420::configureSsd1683()
{
    sendCommand(0x01);
    sendData((HEIGHT - 1) & 0xFF);
    sendData((HEIGHT - 1) >> 8);
    sendData(0x00);

    sendCommand(0x3C);
    sendData(0x05);
    sendCommand(0x18);
    sendData(0x80);
    configureSsdRam();
}

void NMEPD420::configureSsdRam()
{
    sendCommand(0x11);
    sendData(0x03);
    sendCommand(0x44);
    sendData(0x00);
    sendData((WIDTH / 8) - 1);
    sendCommand(0x45);
    sendData(0x00);
    sendData(0x00);
    sendData((HEIGHT - 1) & 0xFF);
    sendData((HEIGHT - 1) >> 8);
    sendCommand(0x4E);
    sendData(0x00);
    sendCommand(0x4F);
    sendData(0x00);
    sendData(0x00);
}

void NMEPD420::updateSsd1683(UpdateTypes type)
{
    configureSsd1683();
    sendCommand(0x24);
    sendData(buffer, BUFFER_SIZE);

    if (type == FULL) {
        configureSsdRam();
        sendCommand(0x26);
        sendRepeated(0x00, BUFFER_SIZE);
        sendCommand(0x22);
        sendData(0xF7);
        sendCommand(0x20);
        beginPolling(100, 5000, UPDATE_TIMEOUT_MS);
    } else {
        sendCommand(0x22);
        sendData(0xDC);
        sendCommand(0x20);
        beginPolling(50, 1000, UPDATE_TIMEOUT_MS);
    }
}

void NMEPD420::finalizeSsd1683()
{
    configureSsdRam();
    sendCommand(0x26);
    sendData(buffer, BUFFER_SIZE);

    if (pinRst != (uint8_t)-1) {
        sendCommand(0x10);
        sendData(0x11);
    }
}

void NMEPD420::configureUc8179()
{
    sendCommand(0x01);
    sendData(0x07);
    sendData(0x07);
    sendData(0x3F);
    sendData(0x3F);
    sendCommand(0x06);
    sendData(0x17);
    sendData(0x17);
    sendData(0x28);
    sendData(0x17);
    sendCommand(0x00);
    sendData(0x0F);
    sendCommand(0x61);
    sendData(WIDTH >> 8);
    sendData(WIDTH & 0xFF);
    sendData(HEIGHT >> 8);
    sendData(HEIGHT & 0xFF);
    sendCommand(0x15);
    sendData(0x00);
    sendCommand(0x50);
    sendData(0x13);
    sendData(0x07);
    sendCommand(0x60);
    sendData(0x22);
}

void NMEPD420::configureUcRam()
{
    sendCommand(0x90);
    sendData(0x00);
    sendData(0x00);
    sendData((WIDTH - 1) >> 8);
    sendData((WIDTH - 1) & 0xFF);
    sendData(0x00);
    sendData(0x00);
    sendData((HEIGHT - 1) >> 8);
    sendData((HEIGHT - 1) & 0xFF);
    sendData(0x00);
}

void NMEPD420::updateUc8179()
{
    configureUc8179();
    configureUcRam();
    sendCommand(0x10);
    sendData(buffer, BUFFER_SIZE);
    configureUcRam();
    sendCommand(0x13);
    sendRepeated(0x00, BUFFER_SIZE);

    sendCommand(0x04);
    waitUntilReady(2000);
    sendCommand(0x12);
    beginPolling(100, 5000, UPDATE_TIMEOUT_MS);
}

void NMEPD420::finalizeUc8179()
{
    sendCommand(0x02);
    waitUntilReady(1000, false);
    if (pinRst != (uint8_t)-1) {
        sendCommand(0x07);
        sendData(0xA5);
    }
}

bool NMEPD420::isUpdateDone()
{
    return digitalRead(pinBusy) != busyActiveLevel;
}

void NMEPD420::finalizeUpdate()
{
    if (controller == Controller::SSD1683)
        finalizeSsd1683();
    else
        finalizeUc8179();
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
