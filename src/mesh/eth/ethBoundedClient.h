#pragma once

#include "configuration.h"

#if HAS_ETHERNET && !defined(USE_WS5500) && !defined(USE_CH390D)

#ifdef USE_ARDUINO_ETHERNET
#include <Ethernet.h>
#else
#include <RAK13800_W5100S.h>
#endif

// The W5x00 libraries wait on the chip with no timeout (flush(), and socketSend() behind write()), so a peer that
// stops acknowledging holds the caller until the watchdog fires. These helpers bound those waits.
static constexpr uint32_t ETH_FLUSH_TIMEOUT_MS = 1000;
static constexpr uint32_t ETH_WRITE_TIMEOUT_MS = 1000;

// Lowers the TCP retry count of every socket on the chip. Ethernet.begin() resets it, so call this after every begin().
void ethLimitTcpRetransmission();

// Bounded flush. Unlike EthernetClient::flush() it returns at once in CLOSE_WAIT with nothing left to read
// (connected() is false there), and leaves the close to stop(), which bounds itself.
void ethFlush(EthernetClient &client, uint32_t timeoutMs = ETH_FLUSH_TIMEOUT_MS);

// Waits, at most timeoutMs, until the send buffer can take len bytes (capped at its size); false on a drop or timeout.
bool ethWaitForWriteSpace(EthernetClient &client, size_t len, uint32_t timeoutMs = ETH_WRITE_TIMEOUT_MS);

// The client MQTT hands to PubSubClient: a write that cannot find room in time fails instead of waiting.
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
