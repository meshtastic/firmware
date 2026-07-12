#include "FakeUART.h"

#ifdef SENSECAP_INDICATOR

FakeUART *FakeSerial = new FakeUART();

FakeUART::FakeUART() {}

void FakeUART::begin(unsigned long baud, uint32_t config, int8_t rxPin, int8_t txPin, bool invert, unsigned long timeout_ms,
                     uint8_t rxfifo_full_thrhd)
{
    baudrate = baud;
    buf_clear();
    LOG_DEBUG("FakeUART::begin(%lu)", baud);
}

void FakeUART::end()
{
    buf_clear();
}

int FakeUART::available()
{
    return buf_avail();
}

int FakeUART::peek()
{
    if (buf_tail == buf_head)
        return -1;
    __sync_synchronize(); // pair with the producer barrier in buf_push
    return buf[buf_tail];
}

int FakeUART::read()
{
    if (buf_tail == buf_head)
        return -1;
    __sync_synchronize(); // pair with the producer barrier in buf_push
    uint8_t ret = buf[buf_tail];
    buf_tail = (buf_tail + 1) % BUF_SIZE;
    return ret;
}

void FakeUART::flush(bool wait)
{
    buf_clear();
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
    // static: ~4.6KB struct, too large for task stacks; only written from
    // the cooperative main loop (GPS thread)
    static meshtastic_InterdeviceMessage message;
    memset(&message, 0, sizeof(message));
    if (size >= sizeof(message.data.nmea)) {
        size = sizeof(message.data.nmea) - 1; // truncate, keep room for the terminator
    }
    memcpy(message.data.nmea, buffer, size);
    message.which_data = meshtastic_InterdeviceMessage_nmea_tag;
    LOG_DEBUG("FakeUART::write(%s)", message.data.nmea);
    sensecapIndicator->send_uplink(message);
    return size;
}

size_t FakeUART::stuff_buffer(const char *buffer, size_t size)
{
    // push buffer byte-wise, stop when full
    for (size_t i = 0; i < size; i++) {
        if (!buf_push(buffer[i])) {
            return i;
        }
    }
    return size;
}

#endif // SENSECAP_INDICATOR