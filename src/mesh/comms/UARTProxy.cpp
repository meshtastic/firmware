#include "UARTProxy.h"

#ifdef SENSECAP_INDICATOR

UARTProxy *uartProxy = new UARTProxy();

UARTProxy::UARTProxy() {}

void UARTProxy::begin(unsigned long baud, uint32_t config, int8_t rxPin, int8_t txPin, bool invert, unsigned long timeout_ms,
                      uint8_t rxfifo_full_thrhd)
{
    baudrate = baud;
    buf_clear();
    LOG_DEBUG("UARTProxy::begin(%lu)", baud);
}

void UARTProxy::end()
{
    buf_clear();
}

int UARTProxy::available()
{
    return buf_avail();
}

int UARTProxy::peek()
{
    if (buf_tail == buf_head)
        return -1;
    __sync_synchronize(); // pair with the producer barrier in buf_push
    return buf[buf_tail];
}

int UARTProxy::read()
{
    if (buf_tail == buf_head)
        return -1;
    __sync_synchronize(); // pair with the producer barrier in buf_push
    uint8_t ret = buf[buf_tail];
    buf_tail = (buf_tail + 1) % BUF_SIZE;
    return ret;
}

void UARTProxy::flush(bool wait)
{
    buf_clear();
}

uint32_t UARTProxy::baudRate()
{
    return baudrate;
}

void UARTProxy::updateBaudRate(unsigned long speed)
{
    baudrate = speed;
}

size_t UARTProxy::setRxBufferSize(size_t size)
{
    return size;
}

size_t UARTProxy::write(const char *buffer)
{
    return write((char *)buffer, strlen(buffer));
}

size_t UARTProxy::write(uint8_t *buffer, size_t size)
{
    return write((char *)buffer, size);
}

size_t UARTProxy::write(char *buffer, size_t size)
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
    LOG_DEBUG("UARTProxy::write %u bytes", (unsigned)size);
    sensecapIndicator->send_uplink(message);
    return size;
}

size_t UARTProxy::stuff_buffer(const char *buffer, size_t size)
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
