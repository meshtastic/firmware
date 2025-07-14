#include "FakeUART.h"

#ifdef SENSECAP_INDICATOR

FakeUART *FakeSerial = new FakeUART();

FakeUART::FakeUART() {}

void FakeUART::begin(unsigned long baud, uint32_t config, int8_t rxPin, int8_t txPin, bool invert, unsigned long timeout_ms,
                     uint8_t rxfifo_full_thrhd)
{
    baudrate = baud;
    FakeBuf.clear();
    LOG_DEBUG("FakeUART::begin(%lu)", baud);
}

void FakeUART::end()
{
    FakeBuf.clear();
}

int FakeUART::available()
{
    return FakeBuf.size();
}

int FakeUART::peek()
{
    unsigned char ret;
    if (FakeBuf.peek(ret))
        return ret;
    return -1;
}

int FakeUART::read()
{
    unsigned char ret;
    if (FakeBuf.pop(ret))
        return ret;
    return -1;
}

void FakeUART::flush(bool wait)
{
    FakeBuf.clear();
}

uint32_t FakeUART::baudRate()
{
    return baudrate;
}

void FakeUART::updateBaudRate(unsigned long speed)
{
    baudrate = speed;
}

size_t FakeUART::setRxBufferSize(size_t size)
{
    return size;
}

size_t FakeUART::write(const char *buffer)
{
    return write((char *)buffer, strlen(buffer));
}

size_t FakeUART::write(uint8_t *buffer, size_t size)
{
    return write((char *)buffer, size);
}

size_t FakeUART::write(char *buffer, size_t size)
{
    meshtastic_InterdeviceMessage message = meshtastic_InterdeviceMessage_init_zero;
    if (size > sizeof(message.data.nmea)) {
        size = sizeof(message.data.nmea); // Truncate if buffer is too large
    }
    memcpy(message.data.nmea, buffer, size);
    message.which_data = meshtastic_InterdeviceMessage_nmea_tag;
    LOG_DEBUG("FakeUART::write(%s)", message.data.nmea);
    sensecapIndicator->send_uplink(message);
    return size;
}

size_t FakeUART::stuff_buffer(const char *buffer, size_t size)
{
    // push buffer in a loop to FakeBuf
    for (size_t i = 0; i < size; i++) {
        if (!FakeBuf.push(buffer[i])) {
            return i;
        }
    }
    return size;
}

#endif // SENSECAP_INDICATOR