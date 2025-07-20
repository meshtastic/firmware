#include "AsyncUDP.h"

#if HAS_ETHERNET

AsyncUDP::AsyncUDP() : OSThread("AsyncUDP"), localPort(0) {}

bool AsyncUDP::listenMulticast(IPAddress multicastIP, uint16_t port, uint8_t ttl)
{
    if (!isMulticast(multicastIP))
        return false;
    localPort = port;
    udp.beginMulticast(multicastIP, port);
    return true;
}

size_t AsyncUDP::write(uint8_t b)
{
    return udp.write(&b, 1);
}

size_t AsyncUDP::write(const uint8_t *data, size_t len)
{
    return udp.write(data, len);
}

void AsyncUDP::onPacket(const std::function<void(AsyncUDPPacket)> &callback)
{
    _onPacket = callback;
}

bool AsyncUDP::writeTo(const uint8_t *data, size_t len, IPAddress ip, uint16_t port)
{
    if (!udp.beginPacket(ip, port))
        return false;
    udp.write(data, len);
    return udp.endPacket();
}

// AsyncUDPPacket
AsyncUDPPacket::AsyncUDPPacket(EthernetUDP &source) : _udp(source), _remoteIP(source.remoteIP()), _remotePort(source.remotePort())
{
    if (_udp.available() > 0) {
        _readLength = _udp.read(_buffer, sizeof(_buffer));
    } else {
        _readLength = 0;
    }
}

IPAddress AsyncUDPPacket::remoteIP()
{
    return _remoteIP;
}

uint16_t AsyncUDPPacket::length()
{
    return _readLength;
}

const uint8_t *AsyncUDPPacket::data()
{
    return _buffer;
}

int32_t AsyncUDP::runOnce()
{
    if (_onPacket && udp.parsePacket() > 0) {
        AsyncUDPPacket packet(udp);
        _onPacket(packet);
    }
    return 5; // check every 5ms
}

#endif // HAS_ETHERNET