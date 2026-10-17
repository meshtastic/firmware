#pragma once

#include "configuration.h"

#if HAS_ETHERNET && !defined(USE_WS5500) && !defined(USE_CH390D)

#ifdef USE_ARDUINO_ETHERNET
#include <Ethernet.h>
#include <utility/w5100.h>
#else
#include <RAK13800_W5100S.h>
#include <w5100.h>
#endif

// arduino-libraries/Ethernet and RAK13800_W5100S both wait on the W5x00 with no timeout of their own:
// - EthernetClient::flush() loops while the socket is ESTABLISHED or CLOSE_WAIT and its send buffer
//   has not drained;
// - socketSend(), behind EthernetClient::write(), loops until the send buffer has room and then
//   until the chip reports SEND_OK, giving up only once the socket has closed.
// If the peer stops acknowledging (a broker that has already dropped us, a browser that went away),
// these waits last as long as the chip keeps retransmitting, about 32 s with its defaults, and the
// caller is reset by the watchdog first. The helpers below bound each of them.

static constexpr uint32_t ETH_FLUSH_TIMEOUT_MS = 1000;
static constexpr uint32_t ETH_WRITE_TIMEOUT_MS = 1000;

// W5x00 TCP retransmission: first retry after 200 ms, each later one twice as long, and the socket
// is dropped after ETH_TCP_RETRY_COUNT retries, 0.2 + 0.4 + 0.8 + 1.6 = 3 s with no acknowledgement.
// That ends socketSend()'s SEND_OK wait well inside the watchdog. The chip default is 8 retries.
static constexpr uint16_t ETH_TCP_RETRY_TIMEOUT_MS = 200;
static constexpr uint8_t ETH_TCP_RETRY_COUNT = 3;

// Ethernet.begin() resets the chip, so call this after every begin().
inline void ethLimitTcpRetransmission()
{
    Ethernet.setRetransmissionTimeout(ETH_TCP_RETRY_TIMEOUT_MS);
    Ethernet.setRetransmissionCount(ETH_TCP_RETRY_COUNT);
}

// EthernetClient::flush() with a time limit: wait for the send buffer to drain while the
// connection is up, but never longer than timeoutMs.
inline void ethFlush(EthernetClient &client, uint32_t timeoutMs = ETH_FLUSH_TIMEOUT_MS)
{
    const uint32_t start = millis();
    while (client.connected() && client.availableForWrite() < (int)W5100.SSIZE) {
        if (millis() - start >= timeoutMs)
            return;
        delay(1);
    }
}

// Wait, at most timeoutMs, until the send buffer can take len bytes (capped at its size, as
// socketSend() caps a single send). False if the link drops or time runs out.
inline bool ethWaitForWriteSpace(EthernetClient &client, size_t len, uint32_t timeoutMs = ETH_WRITE_TIMEOUT_MS)
{
    const size_t need = len < W5100.SSIZE ? len : W5100.SSIZE;
    const uint32_t start = millis();
    while ((size_t)client.availableForWrite() < need) {
        if (!client.connected() || millis() - start >= timeoutMs)
            return false;
        delay(1);
    }
    return true;
}

// The client MQTT hands to PubSubClient, which writes and flushes through it directly: a write
// that cannot find room in time fails instead of waiting, and flush() is bounded.
class BoundedEthernetClient : public EthernetClient
{
  public:
    using EthernetClient::EthernetClient;
    using EthernetClient::write;

    size_t write(uint8_t b) override { return write(&b, 1); }

    size_t write(const uint8_t *buf, size_t size) override
    {
        if (!ethWaitForWriteSpace(*this, size)) {
            setWriteError();
            return 0;
        }
        return EthernetClient::write(buf, size);
    }

    void flush() override { ethFlush(*this); }
};

#endif
