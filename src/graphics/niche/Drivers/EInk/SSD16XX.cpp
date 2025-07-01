#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "./SSD16XX.h"

#include "SPILock.h"

using namespace NicheGraphics::Drivers;

SSD16XX::SSD16XX(uint16_t width, uint16_t height, UpdateTypes supported, uint8_t bufferOffsetX)
    : EInk(width, height, supported), bufferOffsetX(bufferOffsetX)
{
    // Pre-calculate size of the image buffer, for convenience

    // Determine the X dimension of the image buffer, in bytes.
    // Along rows, pixels are stored 8 per byte.
    // Not all display widths are divisible by 8. Need to make sure bytecount accommodates padding for these.
    bufferRowSize = ((width - 1) / 8) + 1;

    // Total size of image buffer, in bytes.
    bufferSize = bufferRowSize * height;
}

void SSD16XX::begin(SPIClass *spi, uint8_t pin_dc, uint8_t pin_cs, uint8_t pin_busy, uint8_t pin_rst)
{
    this->spi = spi;
    this->pin_dc = pin_dc;
    this->pin_cs = pin_cs;
    this->pin_busy = pin_busy;
    this->pin_rst = pin_rst;

    pinMode(pin_dc, OUTPUT);
    pinMode(pin_cs, OUTPUT);
    pinMode(pin_busy, INPUT);

    // If using a reset pin, hold high
    // Reset is active low for Solomon Systech ICs
    if (pin_rst != 0xFF)
        pinMode(pin_rst, INPUT_PULLUP);

    reset();
}

// Poll the displays busy pin until an operation is complete
// Timeout and set fail flag if something went wrong and the display got stuck
void SSD16XX::wait(uint32_t timeout)
{
    // Don't bother waiting if part of the update sequence failed
    // In that situation, we're now just failing-through the process, until we can try again with next update.
    if (failed)
        return;

    uint32_t startMs = millis();

    // Busy when HIGH
    while (digitalRead(pin_busy) == HIGH) {
        // Check for timeout
        if (millis() - startMs > timeout) {
            failed = true;
            break;
        }
        yield();
    }
}

void SSD16XX::reset()
{
    // Check if reset pin is defined
    if (pin_rst != 0xFF) {
        pinMode(pin_rst, OUTPUT);
        digitalWrite(pin_rst, LOW);
        delay(10);
        digitalWrite(pin_rst, HIGH);
        delay(10);
        wait();
    }

    sendCommand(0x12);
    wait();
}

void SSD16XX::sendCommand(const uint8_t command)
{
    // Abort if part of the update sequence failed
    // This will unlock again once we have failed-through the entire process
    if (failed)
        return;

    // Take firmware's SPI lock
    spiLock->lock();

    spi->beginTransaction(spiSettings);
    digitalWrite(pin_dc, LOW); // DC pin low indicates command
    digitalWrite(pin_cs, LOW);
    spi->transfer(command);
    digitalWrite(pin_cs, HIGH);
    digitalWrite(pin_dc, HIGH);
    spi->endTransaction();

    spiLock->unlock();
}

void SSD16XX::sendData(uint8_t data)
{
    sendData(&data, 1);
}

void SSD16XX::sendData(const uint8_t *data, uint32_t size)
{
    // Abort if part of the update sequence failed
    // This will unlock again once we have failed-through the entire process
    if (failed)
        return;

    // Take firmware's SPI lock
    spiLock->lock();

    spi->beginTransaction(spiSettings);
    digitalWrite(pin_dc, HIGH); // DC pin HIGH indicates data, instead of command
    digitalWrite(pin_cs, LOW);

    // Platform-specific SPI command
#if defined(ARCH_ESP32)
    spi->transferBytes(data, NULL, size); // NULL for a "write only" transfer
#elif defined(ARCH_NRF52)
    spi->transfer(data, NULL, size); // NULL for a "write only" transfer
#else
#error Not implemented yet? Feel free to add other platforms here.
#endif

    digitalWrite(pin_cs, HIGH);
    digitalWrite(pin_dc, HIGH);
    spi->endTransaction();

    spiLock->unlock();
}

void SSD16XX::configFullscreen()
{
    // Placing this code in a separate method because it's probably pretty consistent between displays
    // Should make it tidier to override SSD16XX::configure

    // Define the boundaries of the "fullscreen" region, for the controller IC
    static const uint16_t sx = bufferOffsetX; // Notice the offset
    static const uint16_t sy = 0;
    static const uint16_t ex = bufferRowSize + bufferOffsetX - 1; // End is "max index", not "count". Minus 1 handles this
    static const uint16_t ey = height;

    // Split into bytes
    static const uint8_t sy1 = sy & 0xFF;
    static const uint8_t sy2 = (sy >> 8) & 0xFF;
    static const uint8_t ey1 = ey & 0xFF;
    static const uint8_t ey2 = (ey >> 8) & 0xFF;

    // Data entry mode - Left to Right, Top to Bottom
    sendCommand(0x11);
    sendData(0x03);

    // Select controller IC memory region to display a fullscreen image
    sendCommand(0x44); // Memory X start - end
    sendData(sx);
    sendData(ex);
    sendCommand(0x45); // Memory Y start - end
    sendData(sy1);
    sendData(sy2);
    sendData(ey1);
    sendData(ey2);

    // Place the cursor at the start of this memory region, ready to send image data x=0 y=0
    sendCommand(0x4E); // Memory cursor X
    sendData(sx);
    sendCommand(0x4F); // Memory cursor y
    sendData(sy1);
    sendData(sy2);
}

void SSD16XX::update(uint8_t *imageData, UpdateTypes type)
{
    this->updateType = type;
    this->buffer = imageData;

    reset();

    configFullscreen();
    configScanning(); // Virtual, unused by base class
    configVoltages(); // Virtual, unused by base class
    configWaveform(); // Virtual, unused by base class
    wait();

    if (updateType == FULL) {
        writeNewImage();
        writeOldImage();
    } else {
        writeNewImage();
    }

    configUpdateSequence();
    sendCommand(0x20); // Begin executing the update

    // Let the update run async, on display hardware. Base class will poll completion, then finalize.
    // For a blocking update, call await after update
    detachFromUpdate();
}

// Send SPI commands for controller IC to begin executing the refresh operation
void SSD16XX::configUpdateSequence()
{
    switch (updateType) {
    default:
        sendCommand(0x22); // Set "update sequence"
        sendData(0xF7);    // Non-differential, load waveform from OTP
        break;
    }
}

void SSD16XX::writeNewImage()
{
    sendCommand(0x24);
    sendData(buffer, bufferSize);
}

void SSD16XX::writeOldImage()
{
    sendCommand(0x26);
    sendData(buffer, bufferSize);
}

void SSD16XX::detachFromUpdate()
{
    // To save power / cycles, displays can choose to specify an "expected duration" for various refresh types
    // If we know a full-refresh takes at least 4 seconds, we can delay polling until 3 seconds have passed
    // If not implemented, we'll just poll right from the get-go
    switch (updateType) {
    default:
        EInk::beginPolling(100, 0);
    }
}

bool SSD16XX::isUpdateDone()
{
    // Busy when HIGH
    if (digitalRead(pin_busy) == HIGH)
        return false;
    else
        return true;
}

void SSD16XX::finalizeUpdate()
{
    // Put a copy of the image into the "old memory".
    // Used with differential refreshes (e.g. FAST update), to determine which px need to move, and which can remain in place
    // We need to keep the "old memory" up to date, because don't know whether next refresh will be FULL or FAST etc.
    if (updateType != FULL) {
        writeNewImage(); // Only required by some controller variants. Todo: Override just for GDEY0154D678?
        writeOldImage();
        sendCommand(0x7F); // Terminate image write without update
        wait();
    }

    // Enter deep-sleep to save a few µA
    // Waking from this requires that display's reset pin is broken out
    if (pin_rst != 0xFF)
        deepSleep();
}

// Enter a lower-power state
// May only save a few µA..
void SSD16XX::deepSleep()
{
    sendCommand(0x10); // Enter deep sleep
    sendData(0x01);    // Mode 1: preserve image RAM
}
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS