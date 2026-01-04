#ifndef ASYNC_UDP_H
#define ASYNC_UDP_H

#include "configuration.h"

#if HAS_ETHERNET

#include "concurrency/OSThread.h"
#include <IPAddress.h>
#include <Print.h>
#include <RAK13800_W5100S.h>
#include <cstdint>
#include <functional>

class AsyncUDPPacket;

class AsyncUDP : public Print, private concurrency::OSThread
{
  public:
    AsyncUDP();
    explicit operator bool() const { return localPort != 0; }

    bool listenMulticast(IPAddress multicastIP, uint16_t port, uint8_t ttl = 64);
    bool writeTo(const uint8_t *data, size_t len, IPAddress ip, uint16_t port);

    size_t write(uint8_t b) override;
    size_t write(const uint8_t *data, size_t len) override;
    void onPacket(const std::function<void(AsyncUDPPacket)> &callback);

  private:
    EthernetUDP udp;
    uint16_t localPort;
    std::function<void(AsyncUDPPacket)> _onPacket;
    virtual int32_t runOnce() override;
};

class AsyncUDPPacket
{
  public:
    AsyncUDPPacket(EthernetUDP &source);

    IPAddress remoteIP();
    uint16_t length();
    const uint8_t *data();

  private:
    EthernetUDP &_udp;
    IPAddress _remoteIP;
    uint16_t _remotePort;
    size_t _readLength = 0;

    static constexpr size_t BUF_SIZE = 512;
    uint8_t _buffer[BUF_SIZE];
};

inline bool isMulticast(const IPAddress &ip)
{
    return (ip[0] & 0xF0) == 0xE0;
}

#endif // HAS_ETHERNET

#endif // ASYNC_UDP_H
