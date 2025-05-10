#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "./LCMEN2R13EFC1.h"

#include <assert.h>

#include "SPILock.h"

using namespace NicheGraphics::Drivers;

// Look up table: fast refresh, common electrode
static const uint8_t LUT_FAST_VCOMDC[] = {
    0x01, 0x06, 0x03, 0x02, 0x01, 0x01, 0x01, //
    0x01, 0x06, 0x02, 0x01, 0x01, 0x01, 0x01, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
};

// Look up table: fast refresh, pixels which remain white
static const uint8_t LUT_FAST_WW[] = {
    0x01, 0x06, 0x03, 0x02, 0x81, 0x01, 0x01, //
    0x01, 0x06, 0x02, 0x01, 0x01, 0x01, 0x01, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
};

// Look up table: fast refresh, pixel which change from black to white
static const uint8_t LUT_FAST_BW[] = {
    0x01, 0x86, 0x83, 0x82, 0x81, 0x01, 0x01, //
    0x01, 0x86, 0x82, 0x01, 0x01, 0x01, 0x01, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
};

// Look up table: fast refresh, pixels which change from white to black
static const uint8_t LUT_FAST_WB[] = {
    0x01, 0x46, 0x43, 0x02, 0x01, 0x01, 0x01, //
    0x01, 0x46, 0x42, 0x01, 0x01, 0x01, 0x01, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
};

// Look up table: fast refresh, pixels which remain black
static const uint8_t LUT_FAST_BB[] = {
    0x01, 0x06, 0x03, 0x42, 0x41, 0x01, 0x01, //
    0x01, 0x06, 0x02, 0x01, 0x01, 0x01, 0x01, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
};

LCMEN213EFC1::LCMEN213EFC1() : EInk(width, height, supported)
{
    // Pre-calculate size of the image buffer, for convenience

    // Determine the X dimension of the image buffer, in bytes.
    // Along rows, pixels are stored 8 per byte.
    // Not all display widths are divisible by 8. Need to make sure bytecount accommodates padding for these.
    bufferRowSize = ((width - 1) / 8) + 1;

    // Total size of image buffer, in bytes.
    bufferSize = bufferRowSize * height;
}

void LCMEN213EFC1::begin(SPIClass *spi, uint8_t pin_dc, uint8_t pin_cs, uint8_t pin_busy, uint8_t pin_rst)
{
    this->spi = spi;
    this->pin_dc = pin_dc;
    this->pin_cs = pin_cs;
    this->pin_busy = pin_busy;
    this->pin_rst = pin_rst;

    pinMode(pin_dc, OUTPUT);
    pinMode(pin_cs, OUTPUT);
    pinMode(pin_busy, INPUT);

    // Reset is active low, hold high
    pinMode(pin_rst, INPUT_PULLUP);

    reset();
}

// Display an image on the display
void LCMEN213EFC1::update(uint8_t *imageData, UpdateTypes type)
{
    this->updateType = type;
    this->buffer = imageData;

    reset();

    // Config
    if (updateType == FULL)
        configFull();
    else
        configFast();

    // Transfer image data
    if (updateType == FULL) {
        writeNewImage();
        writeOldImage();
    } else {
        writeNewImage();
    }

    sendCommand(0x04); // Power on the panel voltage
    wait();

    sendCommand(0x12); // Begin executing the update

    // Let the update run async, on display hardware. Base class will poll completion, then finalize.
    // For a blocking update, call await after update
    detachFromUpdate();
}

void LCMEN213EFC1::wait()
{
    // Busy when LOW
    while (digitalRead(pin_busy) == LOW)
        yield();
}

void LCMEN213EFC1::reset()
{
    pinMode(pin_rst, OUTPUT);
    digitalWrite(pin_rst, LOW);
    delay(10);
    pinMode(pin_rst, INPUT_PULLUP);
    wait();

    sendCommand(0x12);
    wait();
}

void LCMEN213EFC1::sendCommand(const uint8_t command)
{
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

void LCMEN213EFC1::sendData(uint8_t data)
{
    sendData(&data, 1);
}

void LCMEN213EFC1::sendData(const uint8_t *data, uint32_t size)
{
    // Take firmware's SPI lock
    spiLock->lock();

    spi->beginTransaction(spiSettings);
    digitalWrite(pin_dc, HIGH); // DC pin HIGH indicates data, instead of command
    digitalWrite(pin_cs, LOW);

    // Platform-specific SPI command
    // Mothballing. This display model is only used by Heltec Wireless Paper (ESP32)
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

void LCMEN213EFC1::configFull()
{
    sendCommand(0x00); // Panel setting register
    sendData(0b11 << 6 // Display resolution
             | 1 << 4  // B&W only
             | 1 << 3  // Vertical scan direction
             | 1 << 2  // Horizontal scan direction
             | 1 << 1  // Shutdown: no
             | 1 << 0  // Reset: no
    );

    sendCommand(0x50);     // VCOM and data interval setting register
    sendData(0b10 << 6     // Border driven white
             | 0b11 << 4   // Invert image colors: no
             | 0b0111 << 0 // Interval between VCOM on and image data (default)
    );
}

void LCMEN213EFC1::configFast()
{
    sendCommand(0x00); // Panel setting register
    sendData(0b11 << 6 // Display resolution
             | 1 << 5  // LUT from registers (set below)
             | 1 << 4  // B&W only
             | 1 << 3  // Vertical scan direction
             | 1 << 2  // Horizontal scan direction
             | 1 << 1  // Shutdown: no
             | 1 << 0  // Reset: no
    );

    sendCommand(0x50);     // VCOM and data interval setting register
    sendData(0b11 << 6     // Border floating
             | 0b01 << 4   // Invert image colors: no
             | 0b0111 << 0 // Interval between VCOM on and image data (default)
    );

    // Load the various LUTs
    sendCommand(0x20); // VCOM
    sendData(LUT_FAST_VCOMDC, sizeof(LUT_FAST_VCOMDC));

    sendCommand(0x21); // White -> White
    sendData(LUT_FAST_WW, sizeof(LUT_FAST_WW));

    sendCommand(0x22); // Black -> White
    sendData(LUT_FAST_BW, sizeof(LUT_FAST_BW));

    sendCommand(0x23); // White -> Black
    sendData(LUT_FAST_WB, sizeof(LUT_FAST_WB));

    sendCommand(0x24); // Black -> Black
    sendData(LUT_FAST_BB, sizeof(LUT_FAST_BB));
}

void LCMEN213EFC1::writeNewImage()
{
    sendCommand(0x13);
    sendData(buffer, bufferSize);
}

void LCMEN213EFC1::writeOldImage()
{
    sendCommand(0x10);
    sendData(buffer, bufferSize);
}

void LCMEN213EFC1::detachFromUpdate()
{
    // To save power / cycles, displays can choose to specify an "expected duration" for various refresh types
    // If we know a full-refresh takes at least 4 seconds, we can delay polling until 3 seconds have passed
    // If not implemented, we'll just poll right from the get-go
    switch (updateType) {
    case FULL:
        EInk::beginPolling(10, 3650);
        break;
    case FAST:
        EInk::beginPolling(10, 720);
        break;
    default:
        assert(false);
    }
}

bool LCMEN213EFC1::isUpdateDone()
{
    // Busy when LOW
    if (digitalRead(pin_busy) == LOW)
        return false;
    else
        return true;
}

void LCMEN213EFC1::finalizeUpdate()
{
    // Power off the panel voltages
    sendCommand(0x02);
    wait();

    // Put a copy of the image into the "old memory".
    // Used with differential refreshes (e.g. FAST update), to determine which px need to move, and which can remain in place
    // We need to keep the "old memory" up to date, because don't know whether next refresh will be FULL or FAST etc.
    if (updateType != FULL) {
        writeOldImage();
        wait();
    }
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS