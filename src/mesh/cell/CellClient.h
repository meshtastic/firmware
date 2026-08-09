#pragma once

#include "configuration.h"

#if HAS_CELLULAR

#include <Arduino.h>
#include <Client.h>
#include <functional>
#include <vector>

// Arduino Client over the modem's AT socket API, so PubSubClient and anything else written
// against Client works unmodified. One socket only, and IPv4 only - the AT socket API cannot carry IPv6.

// RX ring capacity. MQTT's largest expected frame plus headroom; overflowing
// drops bytes with a log line rather than truncating silently.
#ifndef CELL_RX_BUFFER_SIZE
#define CELL_RX_BUFFER_SIZE 2048
#endif

class CellClient : public Client
{
  public:
    CellClient() {}
    ~CellClient() override;

    int connect(IPAddress ip, uint16_t port) override;
    int connect(const char *host, uint16_t port) override;

    size_t write(uint8_t b) override;
    size_t write(const uint8_t *buf, size_t size) override;

    int available() override;
    int read() override;
    int read(uint8_t *buf, size_t size) override;
    int peek() override;
    void flush() override;
    void stop() override;
    uint8_t connected() override;
    operator bool() override { return connected(); }

    using Print::write;

  private:
    static void ensureUrcHandlers();

    // Pump the modem until done() or the timeout expires.
    static bool waitFor(std::function<bool()> done, uint32_t timeoutMs);

    // Ask the modem for up to CELL_RX_BUFFER_SIZE pending bytes.
    void fetchPending(uint32_t timeoutMs);

    bool opened = false;
};

#endif
