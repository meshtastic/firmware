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

// EthernetClient::flush() in both arduino-libraries/Ethernet and RAK13800_W5100S waits, with no
// timeout, for the socket's send buffer to drain while the socket is ESTABLISHED or CLOSE_WAIT. If
// the peer stops acknowledging (a broker that has already dropped us, a browser that went away),
// the buffer never drains and the caller spins until the watchdog resets the board.
static constexpr uint32_t ETH_FLUSH_TIMEOUT_MS = 1000;

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

// The client MQTT hands to PubSubClient, which calls flush() itself when it finds the link down.
class BoundedFlushEthernetClient : public EthernetClient
{
  public:
    using EthernetClient::EthernetClient;
    void flush() override { ethFlush(*this); }
};

#endif
